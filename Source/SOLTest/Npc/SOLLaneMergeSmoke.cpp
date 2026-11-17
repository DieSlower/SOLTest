/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLLaneMergeSmoke.h"

#include "Game/SOLGameMode.h"
#include "Menu/SOLMenuText.h"
#include "Npc/SOLNpcSubsystem.h"
#include "Npc/SOLNpcVisuals.h"
#include "OrbitLines/SOLOrbitLineVisuals.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Traffic/SOLLaneTrafficMath.h"
#include "Traffic/SOLLaneTrafficSubsystem.h"
#include "UI/SOLFlightHud.h"
#include "UI/SOLLaneTunnelLabel.h"
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
    // Phase durations (real seconds)
    constexpr double LANEMERGE_WAIT_TIMEOUT_S = 240.0;       // Population spawn and upload
    constexpr double LANEMERGE_READY_SETTLE_S = 2.0;         // The tunnels and labels settle after the population
    constexpr double LANEMERGE_SETTLE_S = 1.2;               // A placement settles (anchor, rebase, the label eases in)
    constexpr double LANEMERGE_WARP_SETTLE_S = 0.15;         // ...under 1000x (short: the lane moves 1000x faster)
    constexpr double LANEMERGE_SHOT_S = 0.8;                 // The screenshot is written before the next move
    constexpr int32 LANEMERGE_MIN_FRAMES = 5;
    constexpr int32 LANEMERGE_WARP_SHOT_FRAMES = 3;          // The warp case's screenshot is captured by then

    // Script values
    constexpr int32 LANEMERGE_SHIPS = 250000;                // Population (any count gives every lane its tunnel)
    constexpr int32 LANEMERGE_WARP_STEPS = 3;                // 1x -> 10x -> 100x -> 1000x
    constexpr float LANEMERGE_MAX_WINDOW_PX = 16384.0f;      // Lifted window size limit for a -ResX / -ResY view

    // The V6 grid (rho 1.5, 0.8, 0.2 x 0.9, 1.0, 1.3 v_t), hidden at rho 2.5, the warp note at 1000x
    constexpr uint8 LANEMERGE_MATCHED = static_cast<uint8>(ESOLLaneMergeVerdict::Matched);
    constexpr uint8 LANEMERGE_TOO_FAST = static_cast<uint8>(ESOLLaneMergeVerdict::TooFast);
    constexpr uint8 LANEMERGE_TOO_SLOW = static_cast<uint8>(ESOLLaneMergeVerdict::TooSlow);
    constexpr uint8 LANEMERGE_GREEN = static_cast<uint8>(ESOLLaneMergeBand::Green);
    constexpr uint8 LANEMERGE_AMBER = static_cast<uint8>(ESOLLaneMergeBand::Amber);
    constexpr uint8 LANEMERGE_RED = static_cast<uint8>(ESOLLaneMergeBand::Red);
    constexpr uint8 LANEMERGE_IN_LABEL = static_cast<uint8>(ESOLLaneMergeWhere::Label);
    constexpr uint8 LANEMERGE_ON_FALLBACK = static_cast<uint8>(ESOLLaneMergeWhere::Fallback);
    constexpr uint8 LANEMERGE_LABEL_OR_Q10 = 0xFF;          // The label, or the fallback line while its label grown
                                                            // by the line fits nowhere or the readout waits to return
                                                            // (the warp view: the lane drifts, the label moves)

    //////////////////////////////////////////////////////////////////////////
    // Sets an integer console variable (sol.SpaceDust)
    void LaneMergeSmokeSetCvar(const TCHAR* name, const int32 value)
    {
        if (IConsoleVariable* cvar = IConsoleManager::Get().FindConsoleVariable(name))
        {
            cvar->Set(value, ECVF_SetByCode);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Sets the -ResX / -ResY view past the desktop size with r.SetRes; nothing without both
    void LaneMergeSmokeApplyCommandLineView(UWorld* world)
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
        window->SetSizeLimits(FWindowSizeLimits().SetMaxWidth(LANEMERGE_MAX_WINDOW_PX)
            .SetMaxHeight(LANEMERGE_MAX_WINDOW_PX));
        GEngine->Exec(world, *FString::Printf(TEXT("r.SetRes %dx%dw"), width, height));
        UE_LOG(LogSOL, Log, TEXT("SmokeLaneMerge: view at %dx%d (window size limit lifted)"), width, height);
    }
}

//////////////////////////////////////////////////////////////////////////
// Caches the subsystems and actors, sets up the overrides, the view and the population
void FSOLLaneMergeSmoke::Start(ASOLShipPawn& pawn)
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
    UE_LOG(LogSOL, Log, TEXT("SmokeLaneMerge: starting (HUD %s, lane table %s)"),
        mHud.IsValid() ? *mHud->GetName() : TEXT("none"), mLaneTraffic.IsValid() ? TEXT("yes") : TEXT("none"));

    // Overrides: the lanes, tunnels and readout leave the player's settings alone; orbit lines and space dust off
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
    LaneMergeSmokeSetCvar(SOL::SpaceDust::CVAR_ENABLED, 0);
    LaneMergeSmokeApplyCommandLineView(world);
    if (mNpcs.IsValid() && mNpcs->GetRequestedCount() == 0)
    {
        mNpcs->RequestNpcCount(LANEMERGE_SHIPS);
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script; returns true once it has finished
bool FSOLLaneMergeSmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    // Name, rho, leg fraction, yaw, speed factor, shown, verdict, band, warp, screenshot, the spot in this view (at the
    // pinned date: beside the wall and at 0.8 Rw no label of the lane is placed, near the axis the label holds it)
    static const FMergeCase CASES[] =
    {
        { TEXT("rho1.5-slow"), 1.5, 0.5, 0.0, 0.9, true, LANEMERGE_TOO_SLOW, LANEMERGE_AMBER, false, false,
            LANEMERGE_ON_FALLBACK },
        { TEXT("rho1.5-matched"), 1.5, 0.5, 0.0, 1.0, true, LANEMERGE_MATCHED, LANEMERGE_GREEN, false, false,
            LANEMERGE_ON_FALLBACK },
        { TEXT("rho1.5-fast"), 1.5, 0.5, 0.0, 1.3, true, LANEMERGE_TOO_FAST, LANEMERGE_RED, false, false,
            LANEMERGE_ON_FALLBACK },
        { TEXT("rho0.8-slow"), 0.8, 0.5, 0.0, 0.9, true, LANEMERGE_TOO_SLOW, LANEMERGE_AMBER, false, true,
            LANEMERGE_ON_FALLBACK },
        { TEXT("rho0.8-matched"), 0.8, 0.5, 0.0, 1.0, true, LANEMERGE_MATCHED, LANEMERGE_GREEN, false, true,
            LANEMERGE_ON_FALLBACK },
        { TEXT("rho0.8-fast"), 0.8, 0.5, 0.0, 1.3, true, LANEMERGE_TOO_FAST, LANEMERGE_RED, false, true,
            LANEMERGE_ON_FALLBACK },
        { TEXT("rho0.2-slow"), 0.2, 0.5, 0.0, 0.9, true, LANEMERGE_TOO_SLOW, LANEMERGE_AMBER, false, false,
            LANEMERGE_IN_LABEL },
        { TEXT("rho0.2-matched"), 0.2, 0.5, 0.0, 1.0, true, LANEMERGE_MATCHED, LANEMERGE_GREEN, false, false,
            LANEMERGE_IN_LABEL },
        { TEXT("rho0.2-fast"), 0.2, 0.5, 0.0, 1.3, true, LANEMERGE_TOO_FAST, LANEMERGE_RED, false, false,
            LANEMERGE_IN_LABEL },
        { TEXT("rho2.5-hidden"), 2.5, 0.5, 0.0, 1.0, false, 0, 0, false, false, LANEMERGE_ON_FALLBACK },
        { TEXT("quarter-axis"), 0.0, 0.25, 0.0, 1.0, true, LANEMERGE_MATCHED, LANEMERGE_GREEN, false, true,
            LANEMERGE_IN_LABEL },
        { TEXT("quarter-wall"), 1.0, 0.25, 1.5, 1.0, true, LANEMERGE_MATCHED, LANEMERGE_GREEN, false, true,
            LANEMERGE_ON_FALLBACK },
        { TEXT("warp-note"), 0.2, 0.5, 0.0, 1.0, true, LANEMERGE_MATCHED, LANEMERGE_GREEN, true, true,
            LANEMERGE_LABEL_OR_Q10 },
    };
    constexpr int32 CASE_COUNT = UE_ARRAY_COUNT(CASES);
    if (!mHud.IsValid() || !mNpcs.IsValid() || !mShips.IsValid() || !mRegistry.IsValid() || !mLaneTraffic.IsValid()
        || !mSimClock.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeLaneMerge: HUD or subsystems missing; aborting"));
        Finish(pawn);
        return true;
    }
    mPhaseTimeS += realDeltaSeconds;
    mTotalTimeS += realDeltaSeconds;
    ++mPhaseFrames;

    // The population and the lane table first (or the timeout), then a short settle
    if (mIsWaiting)
    {
        const bool bReady = mNpcs->GetRails().Num() >= mNpcs->GetRequestedCount()
            && !mLaneTraffic->GetLanes().IsEmpty();
        if (!bReady && mPhaseTimeS < LANEMERGE_WAIT_TIMEOUT_S)
        {
            mPhaseFrames = 0;
            return false;
        }
        if (bReady && mPhaseTimeS < LANEMERGE_READY_SETTLE_S)
        {
            return false;
        }
        mIsWaiting = false;
        mPhaseTimeS = 0.0;
        mPhaseFrames = 0;
        if (!FindLane())
        {
            Check(TEXT("population"), false, FString::Printf(TEXT("%d rails, %d lanes in the table, Earth-Mars lane "
                "not found"), mNpcs->GetRails().Num(), mLaneTraffic->GetLanes().Num()));
            Finish(pawn);
            return true;
        }
    }

    // Each case: place, settle, check (and shoot), let the screenshot be written, next
    const FMergeCase& mergeCase = CASES[mCase];
    if (!mIsPlaced)
    {
        PlaceCase(mergeCase);
        mIsPlaced = true;
        mNoFitFrames = 0;
        mLastNoFitLabelBox = FBox2D(ForceInit);
        mPhaseTimeS = 0.0;
        mPhaseFrames = 0;
        return false;
    }
    const double settleS = mergeCase.bWarp ? LANEMERGE_WARP_SETTLE_S : LANEMERGE_SETTLE_S;
    if (!mIsChecked)
    {
        mNoFitFrames += mHud->GetLastMergeSpot().bNoFit ? 1 : 0;
        for (int32 index = 0; mHud->GetLastMergeSpot().bNoFit && index < mHud->GetLastLaneLabelCount(); ++index)
        {
            mLastNoFitLabelBox = mHud->GetLastLaneLabelLane(index) == mLane ? mHud->GetLastLaneLabelBox(index)
                : mLastNoFitLabelBox;
        }
        if (mPhaseTimeS < settleS || mPhaseFrames < LANEMERGE_MIN_FRAMES)
        {
            return false;
        }
        CheckCase(mergeCase);
        mIsChecked = true;
        mPhaseTimeS = 0.0;
        mPhaseFrames = 0;
        return false;
    }
    if (mergeCase.bWarp && mPhaseFrames >= LANEMERGE_WARP_SHOT_FRAMES)
    {
        // Back to 1x once the warp case's screenshot has captured the warp note
        mSimClock->ResetWarp();
    }
    if (mPhaseTimeS < LANEMERGE_SHOT_S || mPhaseFrames < LANEMERGE_MIN_FRAMES)
    {
        return false;
    }
    mIsPlaced = false;
    mIsChecked = false;
    if (++mCase < CASE_COUNT)
    {
        return false;
    }
    Check(TEXT("V6-spots"), mLabelSpots + mFallbackSpots > 0 && mSpotMisses == 0, FString::Printf(TEXT("shown cases "
        "drawn in the tunnel label %d, on the fallback line under the speed readout %d, at another spot than their "
        "view's %d"), mLabelSpots, mFallbackSpots, mSpotMisses));
    Finish(pawn);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Places the ship at the case's leg fraction and rho, moving along the lane at its speed factor x v_t
void FSOLLaneMergeSmoke::PlaceCase(const FMergeCase& mergeCase)
{
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    const FSOLLaneTrafficEntry& entry = mLaneTraffic->GetLanes()[mLane];
    const FSOLLaneProfile& profile = entry.Profile;
    const FSOLLaneFrame frame = SOLLaneTraffic::MakeLaneFrame(profile, registry.GetPositionM(entry.BodyA),
        registry.GetPositionM(entry.BodyB));
    const double legArcM = mergeCase.LegFraction * frame.CurrentLegM;
    const double alongM = profile.EndArcAM + legArcM;
    const double spanM = (frame.BM - frame.AM).Size();
    const double foot = spanM > 0.0 ? FMath::Clamp(alongM / spanM, 0.0, 1.0) : 0.0;
    const FVector3d positionM = frame.AM + frame.Axis * alongM + frame.Right * (mergeCase.Rho * profile.WallRadiusM);
    const FVector3d frameVelocityMps = registry.GetVelocityMps(entry.BodyA)
        + (registry.GetVelocityMps(entry.BodyB) - registry.GetVelocityMps(entry.BodyA)) * foot;
    const double trafficMps = SOLLaneTraffic::TrafficSpeedMps(profile, frame, legArcM, mergeCase.Rho, 1);
    const FVector3d velocityMps = frameVelocityMps + frame.Axis * (mergeCase.SpeedFactor * trafficMps);

    // The ship's state is in Unreal's axes (EclipticToUnreal mirrors Y and is its own inverse)
    FSOLShipState state = mShips->GetState();
    state.PositionM = SOLRender::EclipticToUnreal(positionM);
    state.VelocityMps = SOLRender::EclipticToUnreal(velocityMps);
    state.AngularVelocityRadPerS = FVector3d::ZeroVector;
    const double yawRad = FMath::DegreesToRadians(mergeCase.YawDeg);
    const FVector3d forward = frame.Axis * FMath::Cos(yawRad) + frame.Right * FMath::Sin(yawRad);
    state.Orientation = UE::Math::TRotationMatrix<double>::MakeFromX(SOLRender::EclipticToUnreal(forward)).ToQuat();
    mShips->SetState(state);
    if (mergeCase.bWarp)
    {
        for (int32 step = 0; step < LANEMERGE_WARP_STEPS; ++step)
        {
            mSimClock->StepWarpUp();
        }
    }
    UE_LOG(LogSOL, Log, TEXT("SmokeLaneMerge: %s placed at rho %.2f, leg %.3f, %.2f x v_t = %.1f m/s along (wall "
        "radius %.0f km, warp %.0fx)"), mergeCase.Name, mergeCase.Rho, mergeCase.LegFraction, mergeCase.SpeedFactor,
        mergeCase.SpeedFactor * trafficMps, profile.WallRadiusM / 1000.0, mSimClock->GetClock().GetWarpFactor());
}

//////////////////////////////////////////////////////////////////////////
// Checks the HUD's readout against the case (shown, verdict, spot, text) and takes the case's screenshot
void FSOLLaneMergeSmoke::CheckCase(const FMergeCase& mergeCase)
{
    const FSOLLaneMergeReadout& readout = mHud->GetLastMergeReadout();
    const FSOLLaneMergeSpot& spot = mHud->GetLastMergeSpot();
    const FString& line = mHud->GetLastMergeLine();
    const FBox2D& box = mHud->GetLastMergeBox();
    FVector2D viewSize(0.0, 0.0);
    if (mHud->GetWorld()->GetGameViewport() != nullptr)
    {
        mHud->GetWorld()->GetGameViewport()->GetViewportSize(viewSize);
    }
    const TCHAR* where = spot.Where == ESOLLaneMergeWhere::Label ? TEXT("label")
        : spot.Where == ESOLLaneMergeWhere::Fallback ? TEXT("fallback") : TEXT("hidden");
    const FString label = FString::Printf(TEXT("V6-%s"), mergeCase.Name);
    if (!mergeCase.bShown)
    {
        Check(*label, !readout.bShown && spot.Where == ESOLLaneMergeWhere::Hidden, FString::Printf(TEXT("readout %s, "
            "drawn %s, rho %.2f, %d tunnel labels drawn"), readout.bShown ? TEXT("shown") : TEXT("hidden"), where,
            readout.Rho, mHud->GetLastLaneLabelCount()));
    }
    else
    {
        // Shown for the lane with the verdict and band, drawn inside the view, the line starting with the traffic
        const bool bOnScreen = box.bIsValid && box.Min.X >= 0.0 && box.Min.Y >= 0.0 && box.Max.X <= viewSize.X
            && box.Max.Y <= viewSize.Y;
        const bool bText = line.StartsWith(SOLMenuText::LANE_TRAFFIC_LABEL, ESearchCase::CaseSensitive)
            && line.Contains(SOLLaneTunnelLabel::MergeVerdictText(static_cast<ESOLLaneMergeVerdict>(mergeCase.Verdict)),
            ESearchCase::CaseSensitive);
        const bool bVerdict = static_cast<uint8>(readout.Verdict) == mergeCase.Verdict
            && static_cast<uint8>(readout.Band) == mergeCase.Band;

        // A fallback line never covers a tunnel label drawn this frame and is used only while no label of the readout's
        // lane is drawn (the settle outlasts the label fade) or that label grown by the line fits nowhere (Q10); a
        // label line lies inside its lane's drawn label box (the box grown by the line, D18); each view draws it at
        // its spot
        const bool bQ10 = spot.Where == ESOLLaneMergeWhere::Fallback && (spot.bNoFit || spot.FitS > 0.0);
        bool bClearOfLabels = true;
        bool bInsideLabel = spot.Where != ESOLLaneMergeWhere::Label;
        const FVector2D slack(1.0, 1.0);
        for (int32 index = 0; index < mHud->GetLastLaneLabelCount(); ++index)
        {
            const FBox2D& labelBox = mHud->GetLastLaneLabelBox(index);
            bClearOfLabels &= spot.Where != ESOLLaneMergeWhere::Fallback || (!labelBox.Intersect(box)
                && (mHud->GetLastLaneLabelLane(index) != mLane || bQ10));
            bInsideLabel |= spot.Where == ESOLLaneMergeWhere::Label && mHud->GetLastLaneLabelLane(index) == mLane
                && FBox2D(labelBox.Min - slack, labelBox.Max + slack).IsInside(box);
        }
        const bool bWhere = static_cast<uint8>(spot.Where) == mergeCase.Where
            || (mergeCase.Where == LANEMERGE_LABEL_OR_Q10 && (spot.Where == ESOLLaneMergeWhere::Label || bQ10));
        Check(*label, readout.bShown && readout.Lane == mLane && bVerdict && spot.Where != ESOLLaneMergeWhere::Hidden
            && bOnScreen && bText && bClearOfLabels && bInsideLabel && bWhere, FString::Printf(TEXT("'%s' lane %d "
            "(expected %d), rho %.3f, traffic %.1f m/s, you %.1f m/s, delta %+.4f, drawn %s at (%.0f, %.0f)-"
            "(%.0f, %.0f) in %.0fx%.0f, %d tunnel labels drawn, %d no-fit frames settling (the last with its label at "
            "(%.0f, %.0f)-(%.0f, %.0f))%s%s%s%s"), *line, readout.Lane, mLane, readout.Rho,
            readout.TrafficSpeedMps, readout.PlayerSpeedMps, readout.Delta, where, box.Min.X, box.Min.Y, box.Max.X,
            box.Max.Y, viewSize.X, viewSize.Y, mHud->GetLastLaneLabelCount(), mNoFitFrames, mLastNoFitLabelBox.Min.X,
            mLastNoFitLabelBox.Min.Y, mLastNoFitLabelBox.Max.X, mLastNoFitLabelBox.Max.Y,
            spot.bNoFit ? TEXT(" (its label placed, grown fits nowhere)")
                : bQ10 ? TEXT(" (its grown label fits again, the readout waits to return)") : TEXT(""),
            bClearOfLabels ? TEXT("") : TEXT(" (OVER A LABEL OR ITS LANE'S LABEL DRAWN)"),
            bInsideLabel ? TEXT("") : TEXT(" (OUTSIDE ITS LABEL)"),
            bWhere ? TEXT("") : mergeCase.Where != LANEMERGE_ON_FALLBACK ? TEXT(" (EXPECTED IN THE LABEL)")
                : TEXT(" (EXPECTED ON THE FALLBACK LINE)")));
        mSpotMisses += bWhere ? 0 : 1;
        mLabelSpots += spot.Where == ESOLLaneMergeWhere::Label ? 1 : 0;
        mFallbackSpots += spot.Where == ESOLLaneMergeWhere::Fallback ? 1 : 0;
        if (mergeCase.bWarp)
        {
            Check(TEXT("V6-warp-note-1000x"), readout.bWarpNote && line.EndsWith(SOLMenuText::LANE_TRAFFIC_WARP_NOTE,
                ESearchCase::CaseSensitive), FString::Printf(TEXT("warp %.0fx, note %s, '%s'"),
                mSimClock->GetClock().GetWarpFactor(), readout.bWarpNote ? TEXT("on") : TEXT("off"), *line));
        }
    }
    if (mergeCase.bShot)
    {
        TakeShot(mergeCase.Name);
    }
}

//////////////////////////////////////////////////////////////////////////
// Finds the Earth - Mars lane in the lane table
bool FSOLLaneMergeSmoke::FindLane()
{
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    const int32 earth = registry.FindByName(FName(SOL::BodyNames::EARTH));
    const int32 mars = registry.FindByName(FName(SOL::BodyNames::MARS));
    const TConstArrayView<FSOLLaneTrafficEntry> lanes = mLaneTraffic->GetLanes();
    mLane = INDEX_NONE;
    for (int32 lane = 0; lane < lanes.Num(); ++lane)
    {
        if (lanes[lane].BodyA == earth && lanes[lane].BodyB == mars && lanes[lane].Profile.bDrivable)
        {
            mLane = lane;
        }
    }
    UE_LOG(LogSOL, Log, TEXT("SmokeLaneMerge: %d rails, %d lanes in the table, Earth-Mars lane %d"),
        mNpcs->GetRails().Num(), lanes.Num(), mLane);
    return mLane != INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Logs the SUMMARY line, restores the warp and the space dust and takes the closing screenshot (which quits)
void FSOLLaneMergeSmoke::Finish(ASOLShipPawn& pawn)
{
    if (mSimClock.IsValid())
    {
        mSimClock->ResetWarp();
    }
    if (mLaneTraffic.IsValid())
    {
        mLaneTraffic->SetReadoutVerificationOverride(false);
    }
    LaneMergeSmokeSetCvar(SOL::SpaceDust::CVAR_ENABLED, 1);
    UE_LOG(LogSOL, Log, TEXT("SmokeLaneMerge SUMMARY: complete after %.1f s, %d/%d checks run passed%s"), mTotalTimeS,
        mChecksPassed, mChecksRun, mChecksPassed == mChecksRun && mChecksRun > 0 ? TEXT("") : TEXT(" (FAILURES)"));
    if (ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>())
    {
        gameMode->TakeSmokeScreenshot();
    }
}

//////////////////////////////////////////////////////////////////////////
// Requests a screenshot with the HUD named LaneMerge_<width>x<height>_<name>.png
void FSOLLaneMergeSmoke::TakeShot(const TCHAR* name) const
{
    FVector2D size(0.0, 0.0);
    if (mHud.IsValid() && mHud->GetWorld()->GetGameViewport() != nullptr)
    {
        mHud->GetWorld()->GetGameViewport()->GetViewportSize(size);
    }
    const FString path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / FString::Printf(
        TEXT("LaneMerge_%dx%d_%s.png"), FMath::RoundToInt32(size.X), FMath::RoundToInt32(size.Y), name));
    FScreenshotRequest::RequestScreenshot(path, true, false);
    UE_LOG(LogSOL, Log, TEXT("SmokeLaneMerge: screenshot %s"), *path);
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLLaneMergeSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SmokeLaneMerge CHECK %-22s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"),
        *detail);
}
