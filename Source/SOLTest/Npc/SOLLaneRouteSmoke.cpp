/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLLaneRouteSmoke.h"

#include "Game/SOLGameMode.h"
#include "Interaction/SOLActionTable.h"
#include "Interaction/SOLInteractMenus.h"
#include "Interaction/SOLInteractionSubsystem.h"
#include "Menu/SOLMenuText.h"
#include "Npc/SOLNpcSubsystem.h"
#include "Npc/SOLNpcVisuals.h"
#include "OrbitLines/SOLOrbitLineVisuals.h"
#include "Ship/SOLAutopilotOrder.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Traffic/SOLLaneNetworkMath.h"
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
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

namespace
{
    // Phase durations and limits (real seconds)
    constexpr double LANEROUTE_WAIT_TIMEOUT_S = 240.0;      // Population spawn, lane table and network
    constexpr double LANEROUTE_READY_SETTLE_S = 2.0;
    constexpr double LANEROUTE_CASE_TIMEOUT_S = 420.0;      // A case that never ends fails
    constexpr double LANEROUTE_SHOT_S = 0.8;                // A screenshot is written before the next case
    constexpr double LANEROUTE_PLACE_SETTLE_S = 1.5;        // The anchor and the previous body positions follow a move
    constexpr double LANEROUTE_PLAN_1X_S = 4.0;             // The 1x plan case flies this long before the map view
    constexpr double LANEROUTE_MAP_SETTLE_S = 2.5;          // The map camera settles
    constexpr int32 LANEROUTE_HUD_FRAMES = 3;               // The HUD draws a state on a later frame
    constexpr int32 LANEROUTE_SKIP_FRAMES = 2;              // An order's first frames read the previous step's values
    constexpr int32 LANEROUTE_PROBE_FRAMES = 3;             // Frames after an order before its plan is read

    // Script values
    constexpr int32 LANEROUTE_SHIPS = 50000;                // Population (any count gives every lane its table row)
    constexpr int32 LANEROUTE_WARP_HOUR = 4;                // 3600x (SOLSimClock's factor table)
    constexpr int32 LANEROUTE_WARP_DAY = 5;                 // 86400x
    constexpr int32 LANEROUTE_WARP_TOP = 7;                 // 2592000x (30 d/s)
    constexpr int32 LANEROUTE_MIN_HOPS = 3;
    constexpr double LANEROUTE_EARTH_OFFSET_M = 3.0e7;      // Placed this far from Earth (inside the at-node radius)
    constexpr double LANEROUTE_PLAN_MAX_SIM_S = 3.0e7;      // A plan must arrive within this (about a year)
    constexpr double LANEROUTE_SUN_MAX_SIM_S = 3.0e6;       // A Sun case must end within this (about 35 days)
    constexpr double LANEROUTE_HELD_MAX_SIM_S = 6.0e6;      // The held closing case ends by this
    constexpr double LANEROUTE_SHIP_SUN_DISTANCE_AU = 0.4;  // The Sun cases start this far from the Sun
    constexpr double LANEROUTE_PATH_OFFSET_RADII = 8.0;     // ...offset so the straight path misses the Sun itself
    constexpr double LANEROUTE_FAST_START_RADII = 60.0;     // The fast Sunward entry: start, aim and speed
    constexpr double LANEROUTE_FAST_AIM_RADII = 15.0;
    constexpr double LANEROUTE_FAST_SPEED_MPS = 1.2e6;
    constexpr double LANEROUTE_FAST_MIN_RADII = 19.0;       // The accepted fast-entry dip bound (SDD 53 6.5, user
                                                            // 2026-11-26): the closest approach stays at or above it
    constexpr double LANEROUTE_EARLY_SAMPLE_MAX_STEP_RADII = 50.0;  // A left-early proximity sample needs a frame under this
    constexpr double LANEROUTE_FAST_FLOOR_RADII = 15.0;    // ...and never below the unpowered line's 15 radii
    constexpr double LANEROUTE_CLOSE_MIN_S = 0.25 * 86400.0;    // A closing lane's close: after this from now...
    constexpr double LANEROUTE_CLOSE_MAX_S = 200.0 * 86400.0;   // ...and within this
    constexpr double LANEROUTE_SLOW_TARGET_RADII = 20.0;    // The warp drops to 3600x this close to the target...
    constexpr double LANEROUTE_SLOW_FINAL_M = 1000.0;       // ...and to 1x this close to its arrival point
    constexpr double LANEROUTE_PARK_AU = 3.0;              // The held case's warp wait parks the ship this far up
    constexpr double LANEROUTE_HELD_LEAD_S = 0.5 * 86400.0;// The held ship is placed this long before the close...
    constexpr double LANEROUTE_HELD_SLOW_S = 3.0 * 86400.0; // ...the wait slows to 3600x this long before it
    constexpr double LANEROUTE_HELD_HOUR_S = 4.0;           // The held case flies this many real seconds at 3600x
    constexpr double LANEROUTE_HELD_MIN_FRACTION = 0.02;   // The held ship's start, clamped to these leg fractions
    constexpr double LANEROUTE_HELD_MAX_FRACTION = 0.9;
    constexpr int32 LANEROUTE_FLIP_AFTER_FRAMES = 5;        // The flip case: detour frames before the override goes on
    constexpr int32 LANEROUTE_FLIP_HOLD_FRAMES = 20;        // ...frames it stays on
    constexpr int32 LANEROUTE_CHASE_HOLD_FRAMES = 10;       // The chase ends after this many frames on the tail
    constexpr float LANEROUTE_MAX_WINDOW_PX = 16384.0f;     // Lifted window size limit for a -ResX / -ResY view

    // The plan targets probed from Earth, in order (the fastest 3+ hop plan is flown)
    const TCHAR* const LANEROUTE_PLAN_TARGETS[] = { SOL::BodyNames::JUPITER, SOL::BodyNames::CERES,
        SOL::BodyNames::SATURN, SOL::BodyNames::URANUS, SOL::BodyNames::NEPTUNE };

    //////////////////////////////////////////////////////////////////////////
    // Sets an integer console variable (sol.SpaceDust)
    void LaneRouteSetCvar(const TCHAR* name, const int32 value)
    {
        if (IConsoleVariable* cvar = IConsoleManager::Get().FindConsoleVariable(name))
        {
            cvar->Set(value, ECVF_SetByCode);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Sets the -ResX / -ResY view past the desktop size with r.SetRes; nothing without both
    void LaneRouteApplyCommandLineView(UWorld* world)
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
        window->SetSizeLimits(FWindowSizeLimits().SetMaxWidth(LANEROUTE_MAX_WINDOW_PX)
            .SetMaxHeight(LANEROUTE_MAX_WINDOW_PX));
        GEngine->Exec(world, *FString::Printf(TEXT("r.SetRes %dx%dw"), width, height));
        UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: view at %dx%d (window size limit lifted)"), width, height);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane mode's name for the log
    const TCHAR* LaneRouteModeName(const ESOLLaneAutoMode mode)
    {
        return mode == ESOLLaneAutoMode::Hold ? TEXT("Hold") : mode == ESOLLaneAutoMode::Approach ? TEXT("Approach")
            : mode == ESOLLaneAutoMode::Left ? TEXT("Left") : TEXT("None");
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a case's name (checks and screenshots)
    const TCHAR* LaneRouteCaseName(const int32 index)
    {
        static const TCHAR* NAMES[] = { TEXT("menus"), TEXT("plan-1x"), TEXT("plan-day"), TEXT("plan-top"),
            TEXT("held-closing"), TEXT("sun-move"), TEXT("sun-override"), TEXT("sun-war"), TEXT("sun-chase"),
            TEXT("sun-target"), TEXT("fast-entry"), TEXT("sun-flip") };
        return index >= 0 && index < static_cast<int32>(UE_ARRAY_COUNT(NAMES)) ? NAMES[index] : TEXT("?");
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a HUD line is a given static text
    bool LaneRouteLineIs(const TCHAR* line, const TCHAR* text)
    {
        return line != nullptr && text != nullptr && FCString::Strlen(text) > 0 && FCString::Strcmp(line, text) == 0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a distance in Sun radii
    double LaneRouteRadii(const double distanceM)
    {
        return distanceM / SOL::SUN_RADIUS_M;
    }
}

//////////////////////////////////////////////////////////////////////////
// Caches the subsystems and actors, sets up the overrides, the view and the population
void FSOLLaneRouteSmoke::Start(ASOLShipPawn& pawn)
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
    mInteraction = world->GetSubsystem<USOLInteractionSubsystem>();
    UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: starting (HUD %s, lane table %s)"),
        mHud.IsValid() ? *mHud->GetName() : TEXT("none"), mLaneTraffic.IsValid() ? TEXT("yes") : TEXT("none"));

    // Overrides: the lanes leave the player's settings alone; orbit lines and space dust off
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
    LaneRouteSetCvar(SOL::SpaceDust::CVAR_ENABLED, 0);
    LaneRouteApplyCommandLineView(world);
    pawn.SetFlightCursorScripted(true);
    if (mNpcs.IsValid() && mNpcs->GetRequestedCount() == 0)
    {
        mNpcs->RequestNpcCount(LANEROUTE_SHIPS);
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script; returns true once it has finished
bool FSOLLaneRouteSmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    if (!mHud.IsValid() || !mNpcs.IsValid() || !mShips.IsValid() || !mRegistry.IsValid() || !mLaneTraffic.IsValid()
        || !mSimClock.IsValid() || !mInteraction.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeLaneRoute: HUD or subsystems missing; aborting"));
        Finish(pawn);
        return true;
    }
    mPhaseTimeS += realDeltaSeconds;
    mTotalTimeS += realDeltaSeconds;
    mStageRealS += realDeltaSeconds;
    mCaseRealS += realDeltaSeconds;
    ++mStageFrames;

    // The population, the lane table and the network first (or the timeout), then a short settle
    if (mIsWaiting)
    {
        const bool bReady = mNpcs->GetRails().Num() >= mNpcs->GetRequestedCount()
            && !mLaneTraffic->GetLanes().IsEmpty() && mLaneTraffic->GetNetwork().IsBuilt();
        if ((!bReady && mPhaseTimeS < LANEROUTE_WAIT_TIMEOUT_S) || (bReady && mPhaseTimeS < LANEROUTE_READY_SETTLE_S))
        {
            return false;
        }
        mIsWaiting = false;
        const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
        mSun = registry.FindByName(FName(SOL::BodyNames::SUN));
        mMercury = registry.FindByName(FName(SOL::BodyNames::MERCURY));
        mVenus = registry.FindByName(FName(SOL::BodyNames::VENUS));
        mEarth = registry.FindByName(FName(SOL::BodyNames::EARTH));
        mMars = registry.FindByName(FName(SOL::BodyNames::MARS));
        if (!bReady || mSun == INDEX_NONE || mMercury == INDEX_NONE || mVenus == INDEX_NONE || mEarth == INDEX_NONE
            || mMars == INDEX_NONE)
        {
            Check(TEXT("population"), false, FString::Printf(TEXT("%d rails, %d lanes, network %s, bodies %d %d %d %d "
                "%d"), mNpcs->GetRails().Num(), mLaneTraffic->GetLanes().Num(), mLaneTraffic->GetNetwork().IsBuilt()
                ? TEXT("built") : TEXT("NOT BUILT"), mSun, mMercury, mVenus, mEarth, mMars));
            Finish(pawn);
            return true;
        }
        UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: ready, %d rails, %d lanes, %d nodes, epoch %lld"),
            mNpcs->GetRails().Num(), mLaneTraffic->GetLanes().Num(),
            mLaneTraffic->GetNetwork().GetConfig().Nodes.Num(), mLaneTraffic->GetNetwork().GetCurrentEpoch());
    }

    // Each case: start, run to its end, let its screenshot be written, next
    if (!mIsCaseStarted)
    {
        // -SOLLaneRouteCases=<names> (comma-separated case names) runs only those cases
        FString only;
        if (FParse::Value(FCommandLine::Get(), SOL::CommandLine::SMOKE_LANE_ROUTE_CASES, only, false) && !only.IsEmpty())
        {
            TArray<FString> names;
            only.ParseIntoArray(names, TEXT(","));
            while (mCase != ECase::Count && !names.Contains(LaneRouteCaseName(static_cast<int32>(mCase))))
            {
                mCase = static_cast<ECase>(static_cast<int32>(mCase) + 1);
            }
            if (mCase == ECase::Count)
            {
                Finish(pawn);
                return true;
            }
        }
        mIsCaseStarted = true;
        mStage = 0;
        mStageRealS = 0.0;
        mStageFrames = 0;
        mCaseRealS = 0.0;
        mPhaseTimeS = 0.0;
        ResetObservations();
        UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: case %s"), LaneRouteCaseName(static_cast<int32>(mCase)));
    }
    if (!mIsShotTaken)
    {
        if (UpdateCase(pawn))
        {
            mIsShotTaken = true;
            mPhaseTimeS = 0.0;
        }
        return false;
    }
    if (mPhaseTimeS < LANEROUTE_SHOT_S)
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
// Runs one frame of the current case; returns true when it has finished (its checks logged)
bool FSOLLaneRouteSmoke::UpdateCase(ASOLShipPawn& pawn)
{
    switch (mCase)
    {
    case ECase::Menus:
        return UpdateMenus(pawn);
    case ECase::Plan1x:
        return UpdatePlan(pawn, 0);
    case ECase::PlanDay:
        return UpdatePlan(pawn, LANEROUTE_WARP_DAY);
    case ECase::PlanTop:
        return UpdatePlan(pawn, LANEROUTE_WARP_TOP);
    case ECase::HeldClosing:
        return UpdateHeldClosing();
    default:
        return UpdateSun();
    }
}

//////////////////////////////////////////////////////////////////////////
// The radial menu case: a body's menu has 11 sectors; its Toggle Sun Safety row switches the override on and off
bool FSOLLaneRouteSmoke::UpdateMenus(ASOLShipPawn& pawn)
{
    // Finds a body icon whose row is full (Star, Planet, Moon or Dwarf: the rows that carry the override)
    const auto findBody = [this](FSOLObjectRef& outObject, FVector2D& outCenterPx)
    {
        for (const FSOLIconHit& hit : mInteraction->GetIconHits())
        {
            if (hit.Object.Kind == ESOLObjectKind::Body && hit.Object.Index != mEarth
                && SOLActionTable::RowCount(mInteraction->GetObjectType(hit.Object)) == SOL::Interact::MAX_ACTIONS)
            {
                outObject = hit.Object;
                outCenterPx = hit.CenterPx;
                return true;
            }
        }
        return false;
    };
    // Presses Fire on a body icon and drags onto the override's sector (the radial menu opens on the press)
    const auto pressAndDrag = [this, &pawn](const FVector2D& centerPx)
    {
        pawn.HandleFlightCursorMoved(centerPx);
        mInteraction->UpdateHover(pawn.GetFlightCursorPx(), true);
        pawn.HandleFire(true);
        const FSOLRadialMenuState& radial = mInteraction->GetRadial();
        const FSOLActionList& actions = mInteraction->GetRadialActions();
        const float uiScale = mHud->GetUiScale();
        for (int32 index = 0; radial.bOpen && index < actions.Num; ++index)
        {
            if (actions.Entries[index].Action == ESOLAction::SunSafetyOverride)
            {
                const double angle = SOLRadialMenu::SectorMidAngleRad(index, radial.Count);
                const double radiusPx = 0.5 * (SOL::Interact::RADIAL_INNER_PX + SOL::Interact::RADIAL_OUTER_PX)
                    * uiScale;
                pawn.HandleFlightCursorMoved(radial.CenterPx + FVector2D(FMath::Cos(angle), FMath::Sin(angle))
                    * radiusPx);
                return true;
            }
        }
        return false;
    };
    // Releases Fire where the cursor is (runs the highlighted row)
    const auto release = [this, &pawn]()
    {
        mInteraction->MovePointer(pawn.GetFlightCursorPx());
        pawn.HandleFire(false);
    };
    // Advances to the next stage and restarts its timers
    const auto nextStage = [this]()
    {
        ++mStage;
        mStageRealS = 0.0;
        mStageFrames = 0;
    };

    FSOLObjectRef body;
    FVector2D centerPx(0.0, 0.0);
    switch (mStage)
    {
    case 0:
        // Near Earth, nose to Mars so planet icons are on screen; the safety on
        SetWarpIndex(0);
        SetOverride(false);
        PlaceNearEarth();
        Place(mShips->GetState().PositionM, BodyMps(mEarth), BodyM(mMars) - mShips->GetState().PositionM);
        nextStage();
        return false;
    case 1:
        if (mStageRealS < LANEROUTE_PLACE_SETTLE_S)
        {
            return false;
        }
        if (!findBody(body, centerPx))
        {
            if (mStageRealS > 4.0 * LANEROUTE_PLACE_SETTLE_S)
            {
                Check(TEXT("menus-radial"), false, FString::Printf(TEXT("no full-row body icon on screen (%d hits)"),
                    mInteraction->GetIconHits().Num()));
                return true;
            }
            return false;
        }
        UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: radial on body %d (%s) at (%.0f, %.0f)"), body.Index,
            *mRegistry->GetRegistry().GetName(body.Index).ToString(), centerPx.X, centerPx.Y);
        if (!pressAndDrag(centerPx))
        {
            Check(TEXT("menus-radial"), false, FString::Printf(TEXT("the radial %s, no Toggle Sun Safety sector"),
                mInteraction->IsRadialOpen() ? TEXT("opened") : TEXT("DID NOT OPEN")));
            release();
            return true;
        }
        nextStage();
        return false;
    case 2:
        if (mStageFrames < LANEROUTE_HUD_FRAMES)
        {
            return false;
        }
        TakeShot(TEXT("radial"));
        Check(TEXT("menus-radial"), mInteraction->IsRadialOpen() && mInteraction->GetRadial().Count
            == SOL::Interact::MAX_ACTIONS && mHud->GetLastRadialSectorCount() == SOL::Interact::MAX_ACTIONS
            && mInteraction->GetRadial().Highlighted != INDEX_NONE, FString::Printf(TEXT("radial %s, %d entries, %d "
            "sectors drawn (MAX_ACTIONS %d), highlighted %d"), mInteraction->IsRadialOpen() ? TEXT("open")
            : TEXT("CLOSED"), mInteraction->GetRadial().Count, mHud->GetLastRadialSectorCount(),
            SOL::Interact::MAX_ACTIONS, mInteraction->GetRadial().Highlighted));
        nextStage();
        return false;
    case 3:
        if (mStageRealS < LANEROUTE_SHOT_S)
        {
            return false;
        }
        release();
        nextStage();
        return false;
    case 4:
    {
        if (mStageFrames < LANEROUTE_HUD_FRAMES)
        {
            return false;
        }
        // On through the row, shown with no order; then off again through the row
        const TCHAR* line = mHud->GetLastSunSafetyLine();
        Check(TEXT("menus-override-on"), mShips->IsSunSafetyOverridden() && mShips->GetAutopilotBodyIndex()
            == INDEX_NONE && LaneRouteLineIs(line, SOLAutopilotOrder::SunSafetyLineText(true)), FString::Printf(
            TEXT("override %s, order %d, HUD line '%s'"), mShips->IsSunSafetyOverridden() ? TEXT("on") : TEXT("OFF"),
            mShips->GetAutopilotBodyIndex(), line));
        TakeShot(TEXT("safety-off-no-order"));
        if (findBody(body, centerPx) && pressAndDrag(centerPx))
        {
            release();
        }
        nextStage();
        return false;
    }
    default:
    {
        if (mStageFrames < LANEROUTE_HUD_FRAMES)
        {
            return false;
        }
        const TCHAR* line = mHud->GetLastSunSafetyLine();
        Check(TEXT("menus-override-off"), !mShips->IsSunSafetyOverridden() && FCString::Strlen(line) == 0,
            FString::Printf(TEXT("override %s, HUD line '%s'"), mShips->IsSunSafetyOverridden() ? TEXT("STILL ON")
            : TEXT("off"), line));
        SetOverride(false);
        return true;
    }
    }
}

//////////////////////////////////////////////////////////////////////////
// A plan case: probe the targets from Earth for the fastest 3+ hop plan, fly it at a warp, check hops, lanes, arrival
bool FSOLLaneRouteSmoke::UpdatePlan(ASOLShipPawn& pawn, const int32 warpIndex)
{
    const TCHAR* name = LaneRouteCaseName(static_cast<int32>(mCase));
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    // Advances to the next stage and restarts its timers
    const auto nextStage = [this]()
    {
        ++mStage;
        mStageRealS = 0.0;
        mStageFrames = 0;
    };
    switch (mStage)
    {
    case 0:
        SetWarpIndex(0);
        SetOverride(false);
        PlaceNearEarth();
        mProbe = 0;
        mPlanTarget = INDEX_NONE;
        mPlanHops = 0;
        mPlanTimeS = 0.0;
        nextStage();
        return false;
    case 1:
    case 3:
        // The anchor and the previous body positions follow the move
        if (mStageRealS >= LANEROUTE_PLACE_SETTLE_S)
        {
            nextStage();
        }
        return false;
    case 2:
        // Each candidate: order it from the settled place, read its plan a few frames later
        if (mProbe < static_cast<int32>(UE_ARRAY_COUNT(LANEROUTE_PLAN_TARGETS)))
        {
            const int32 target = registry.FindByName(FName(LANEROUTE_PLAN_TARGETS[mProbe]));
            const int32 cycleFrame = (mStageFrames - 1) % (LANEROUTE_PROBE_FRAMES + 2);
            if (cycleFrame == 0)
            {
                mShips->EngageAutopilot(target, false);
            }
            else if (cycleFrame == LANEROUTE_PROBE_FRAMES + 1)
            {
                const FSOLLaneRoutePlan plan = mShips->GetAutopilotLanePlan();
                double timeS = 0.0;
                for (int32 hop = 0; hop < plan.HopCount; ++hop)
                {
                    timeS += plan.Hops[hop].TimeS;
                }
                UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: %s probe %s: %d hops, %.1f days"), name,
                    LANEROUTE_PLAN_TARGETS[mProbe], plan.HopCount, timeS / 86400.0);
                if (plan.HopCount >= LANEROUTE_MIN_HOPS && (mPlanTarget == INDEX_NONE || timeS < mPlanTimeS))
                {
                    mPlanTarget = target;
                    mPlanHops = plan.HopCount;
                    mPlanTimeS = timeS;
                }
                ++mProbe;
            }
            return false;
        }
        mShips->DisengageAutopilot();
        if (mPlanTarget == INDEX_NONE)
        {
            Check(*FString::Printf(TEXT("%s-plan"), name), false, TEXT("no target with a 3+ hop plan from Earth"));
            return true;
        }
        PlaceNearEarth();
        nextStage();
        return false;
    case 4:
        mShips->EngageAutopilot(mPlanTarget, false);
        SetWarpIndex(warpIndex);
        mCaseSimStartS = mSimClock->GetClock().GetSecondsSinceJ2000();
        UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: %s flies to %s (%d hops, %.1f days planned) at warp index %d"), name,
            *registry.GetName(mPlanTarget).ToString(), mPlanHops, mPlanTimeS / 86400.0, warpIndex);
        nextStage();
        return false;
    case 5:
    {
        Observe();
        const double simS = mSimClock->GetClock().GetSecondsSinceJ2000() - mCaseSimStartS;
        const bool bTimedOut = mCaseRealS > LANEROUTE_CASE_TIMEOUT_S || simS > LANEROUTE_PLAN_MAX_SIM_S;
        if (warpIndex == 0)
        {
            // 1x: a 3+ hop trip cannot arrive in a smoke; the plan, the VIA line and the first hop's lane only
            if (mStageRealS < LANEROUTE_PLAN_1X_S && !bTimedOut)
            {
                return false;
            }
            const FSOLLaneRoutePlan plan = mShips->GetAutopilotLanePlan();
            const int32 viaBody = mShips->GetAutopilotViaBodyIndex();
            Check(TEXT("plan-1x-plan"), mMaxHopCount >= LANEROUTE_MIN_HOPS && SOLLaneRoutePlan::HasPlan(plan),
                FString::Printf(TEXT("plan %d hops (current %d, serial %u)"), plan.HopCount, plan.CurrentHop,
                plan.Serial));
            Check(TEXT("plan-1x-via"), mViaFrames > LANEROUTE_HUD_FRAMES && viaBody != INDEX_NONE
                && mHud->GetLastViaBody() == viaBody, FString::Printf(TEXT("VIA %s on %d frames (HUD %d)"),
                viaBody != INDEX_NONE ? *registry.GetName(viaBody).ToString() : TEXT("none"), mViaFrames,
                mHud->GetLastViaBody()));
            Check(TEXT("plan-1x-lane"), mHasSeenLaneLatch && mWrongLaneFrames == 0, FString::Printf(TEXT("lane %s, "
                "%d frames on a lane other than the hop's"), mHasSeenLaneLatch ? TEXT("latched") : TEXT("NOT LATCHED"),
                mWrongLaneFrames));
            pawn.HandleOpenMap(false);
            nextStage();
            return false;
        }
        SlowNearTarget(mPlanTarget);
        mHasArrived |= mOrderFrames > LANEROUTE_SKIP_FRAMES && mShips->GetAutopilotBodyIndex() == INDEX_NONE;
        if (!mHasArrived && !bTimedOut)
        {
            return false;
        }
        const double distanceM = FVector3d::Dist(mShips->GetState().PositionM, BodyM(mPlanTarget));
        {
            // The order's state at the end (an order that never arrives is diagnosed from this line)
            const FSOLGuidanceOutput guidance = mShips->GetAutopilotGuidance();
            const FSOLLaneRoutePlan plan = mShips->GetAutopilotLanePlan();
            UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: %s end: order %d, phase %d, distance %.4g km, closing %.1f m/s, "
                "relative speed %.1f m/s, ETA %.0f s, lane mode %s, plan %d/%d%s, detour %s, precision reduced %s"),
                name, mShips->GetAutopilotBodyIndex(), static_cast<int32>(guidance.Phase), guidance.DistanceM / 1000.0,
                guidance.ClosingSpeedMps, (mShips->GetState().VelocityMps - BodyMps(mPlanTarget)).Size(),
                guidance.EtaS, LaneRouteModeName(mShips->GetAutopilotLaneMode()), plan.CurrentHop, plan.HopCount,
                plan.bHopLeftEarly ? TEXT(" left early") : TEXT(""), mShips->IsAutopilotSunDetour() ? TEXT("on")
                : TEXT("off"), mShips->IsAutopilotPrecisionReduced() ? TEXT("yes") : TEXT("no"));
        }
        Check(*FString::Printf(TEXT("%s-hops"), name), mMaxHopCount >= LANEROUTE_MIN_HOPS
            && mHopChanges >= LANEROUTE_MIN_HOPS - 1 && mWrongLaneFrames == 0 && mHopsEndedAway == 0
            && mEarlyHopsUnreached == 0 && mEarlyPendingBody == INDEX_NONE && mHasArrived,
            FString::Printf(TEXT("plan up to %d hops, %d lane changes, %d re-plans, %d frames on a lane other than the "
            "hop's, %d hops ended or dropped away from their end node without holding their lane (hops left early "
            "excluded), %d hops left early ended via FinishEarlyHop (%d reached their end node within the sampled "
            "radius, %d did not, %d not sampled: one frame too large), VIA on %d frames"), mMaxHopCount,
            mHopChanges, mReplans, mWrongLaneFrames, mHopsEndedAway, mEarlyHopsEnded, mEarlyHopsReached,
            mEarlyHopsUnreached + (mEarlyPendingBody != INDEX_NONE ? 1 : 0), mEarlyProximitySkipped, mViaFrames));
        if (warpIndex == LANEROUTE_WARP_DAY)
        {
            Check(TEXT("plan-day-epoch"), mHeldEpochChanges > 0 && mWrongLaneFrames == 0, FString::Printf(TEXT(
                "%d lane epochs changed while a hop was held"), mHeldEpochChanges));
        }
        Check(*FString::Printf(TEXT("%s-arrived"), name), mHasArrived, FString::Printf(TEXT("%s after %.1f days sim, "
            "%.4g km from %s"), mHasArrived ? TEXT("arrived") : TEXT("NOT ARRIVED"), simS / 86400.0,
            distanceM / 1000.0, *registry.GetName(mPlanTarget).ToString()));
        return true;
    }
    case 6:
    {
        // The map: the plan polyline drawn
        if (mStageRealS < LANEROUTE_MAP_SETTLE_S)
        {
            return false;
        }
        FVector3d pointsM[SOL::NPC_LANE_ROUTE_MAX_HOPS + 1];
        const int32 points = mShips->GetAutopilotLaneRoutePolylineM(pointsM);
        // No player route or draft exists, so the overlay's triangles and dashes are the plan's
        const FSOLMapRouteOverlay& overlay = mHud->GetRouteOverlay();
        Check(TEXT("plan-1x-map"), points >= LANEROUTE_MIN_HOPS + 1 && overlay.GetLastLegsDrawn() == 0
            && overlay.GetLastTriangles() > 0 && overlay.GetLastDashesDrawn() > 0, FString::Printf(TEXT("polyline %d "
            "points; overlay %d route legs, %d triangles, %d dashes, %d arrows"), points, overlay.GetLastLegsDrawn(),
            overlay.GetLastTriangles(), overlay.GetLastDashesDrawn(), overlay.GetLastArrowsDrawn()));
        TakeShot(TEXT("map-plan"));
        nextStage();
        return false;
    }
    case 7:
        if (mStageRealS < LANEROUTE_SHOT_S)
        {
            return false;
        }
        pawn.HandleCloseMap();
        nextStage();
        return false;
    case 8:
    {
        if (mStageRealS < 1.0)
        {
            return false;
        }
        // Manual takeover: thrust forward cancels the order
        FSOLPilotInput input;
        input.Thrust = FVector3d(1.0, 0.0, 0.0);
        const bool bTook = mShips->ApplyPilotInput(input);
        UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: manual takeover %s"), bTook ? TEXT("took control") : TEXT("REFUSED"));
        nextStage();
        return false;
    }
    default:
    {
        if (mStageFrames < LANEROUTE_HUD_FRAMES + 2)
        {
            return false;
        }
        FVector3d pointsM[SOL::NPC_LANE_ROUTE_MAX_HOPS + 1];
        const int32 points = mShips->GetAutopilotLaneRoutePolylineM(pointsM);
        Check(TEXT("plan-1x-takeover"), mShips->GetAutopilotBodyIndex() == INDEX_NONE
            && mShips->GetAutopilotViaBodyIndex() == INDEX_NONE && mHud->GetLastViaBody() == INDEX_NONE && points == 0
            && !SOLLaneRoutePlan::HasPlan(mShips->GetAutopilotLanePlan()), FString::Printf(TEXT("order %d, VIA %d (HUD "
            "%d), polyline %d points, plan %d hops"), mShips->GetAutopilotBodyIndex(),
            mShips->GetAutopilotViaBodyIndex(), mHud->GetLastViaBody(), points,
            mShips->GetAutopilotLanePlan().HopCount));
        return true;
    }
    }
}

//////////////////////////////////////////////////////////////////////////
// The held lane closing case: holding a lane whose window closes for the Sun before the hop ends, AUTO leaves it with
// the notice and never comes inside 20 Sun radii
bool FSOLLaneRouteSmoke::UpdateHeldClosing()
{
    // Advances to the next stage and restarts its timers
    const auto nextStage = [this]()
    {
        ++mStage;
        mStageRealS = 0.0;
        mStageFrames = 0;
    };
    const FSOLLaneTrafficEntry* const entry = mLaneTraffic->GetLanes().IsValidIndex(mClosingEntry)
        ? &mLaneTraffic->GetLanes()[mClosingEntry] : nullptr;
    const double nowS = mSimClock->GetClock().GetSecondsSinceJ2000();
    switch (mStage)
    {
    case 0:
        // Parked far above the ecliptic (no body to fall onto during the warp wait)
        SetWarpIndex(0);
        SetOverride(false);
        Place(FVector3d(0.0, 0.0, LANEROUTE_PARK_AU * SOL::AU_M), FVector3d::ZeroVector, FVector3d(1.0, 0.0, 0.0));
        if (!FindSunClosingLane())
        {
            Check(TEXT("held-closing-lane"), false, TEXT("no open lane closing for the Sun in the schedule's horizon"));
            return true;
        }
        nextStage();
        return false;
    case 1:
        // Warp (no order) until the close is near (the line already close to the Sun), then place the ship holding
        // the lane
        if (entry == nullptr)
        {
            Check(TEXT("held-closing-lane"), false, TEXT("the lane left the table"));
            return true;
        }
        if (nowS < mClosingCloseS - LANEROUTE_HELD_LEAD_S && mCaseRealS < LANEROUTE_CASE_TIMEOUT_S)
        {
            const int32 warp = nowS < mClosingCloseS - LANEROUTE_HELD_SLOW_S ? LANEROUTE_WARP_DAY : LANEROUTE_WARP_HOUR;
            if (mSimClock->GetClock().GetWarpIndex() != warp)
            {
                SetWarpIndex(warp);
            }
            return false;
        }
        SetWarpIndex(0);
        nextStage();
        return false;
    case 2:
    {
        if (entry == nullptr)
        {
            Check(TEXT("held-closing-lane"), false, TEXT("the lane left the table"));
            return true;
        }
        double openS = 0.0;
        double closeS = 0.0;
        const bool bWindow = entry->Slot != INDEX_NONE && mLaneTraffic->GetNetwork().GetSlotWindow(entry->Slot, openS,
            closeS);
        const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
        const FSOLLaneFrame frame = SOLLaneTraffic::MakeLaneFrame(entry->Profile, registry.GetPositionM(entry->BodyA),
            registry.GetPositionM(entry->BodyB));

        // Halfway from A's end ring to the lane's closest point to the Sun, so the rest of the hop runs past the Sun
        const double spanM = (frame.BM - frame.AM).Size();
        const double closestAlongM = spanM > 0.0 ? FMath::Clamp(-frame.AM.Dot(frame.BM - frame.AM) / spanM, 0.0, spanM)
            : 0.0;
        const double closestLeg = (closestAlongM - entry->Profile.EndArcAM) / FMath::Max(frame.CurrentLegM, 1.0);
        const double placeLeg = FMath::Clamp(0.5 * closestLeg, LANEROUTE_HELD_MIN_FRACTION, LANEROUTE_HELD_MAX_FRACTION);
        const double legArcM = placeLeg * frame.CurrentLegM;
        const double alongM = entry->Profile.EndArcAM + legArcM;
        const double foot = spanM > 0.0 ? FMath::Clamp(alongM / spanM, 0.0, 1.0) : 0.0;
        const FVector3d positionM = frame.AM + frame.Axis * alongM
            + frame.Right * (SOL::NPC_LANE_AUTO_TRACK_WALL_RADII * entry->Profile.WallRadiusM);
        const FVector3d frameMps = registry.GetVelocityMps(entry->BodyA) + (registry.GetVelocityMps(entry->BodyB)
            - registry.GetVelocityMps(entry->BodyA)) * foot;
        const double trafficMps = SOLLaneTraffic::TrafficSpeedMps(entry->Profile, frame, legArcM,
            SOL::NPC_LANE_AUTO_TRACK_WALL_RADII, 1);
        Place(SOLRender::EclipticToUnreal(positionM), SOLRender::EclipticToUnreal(frameMps + frame.Axis * trafficMps),
            SOLRender::EclipticToUnreal(frame.Axis));
        mShips->EngageAutopilot(entry->BodyB, false);
        SetWarpIndex(LANEROUTE_WARP_HOUR);
        mCaseSimStartS = nowS;
        UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: held-closing on lane %d (%s - %s), open %s, closes in %.2f days, "
            "placed at leg %.3f (closest to the Sun at leg %.3f, %.2f Sun radii now), %.1f m/s traffic"), mClosingEntry,
            *registry.GetName(entry->BodyA).ToString(), *registry.GetName(entry->BodyB).ToString(),
            entry->bOpenNow ? TEXT("now") : TEXT("NOT NOW"), bWindow ? (closeS - nowS) / 86400.0 : -1.0, placeLeg,
            closestLeg, LaneRouteRadii(SOLLaneNetwork::SunSegmentDistanceM(frame.AM, frame.BM)), trafficMps);
        nextStage();
        return false;
    }
    default:
    {
        Observe();
        const double simS = nowS - mCaseSimStartS;
        // The leave happens at 3600x; the rest of the trip at 86400x until near the end body
        if (mStageRealS > LANEROUTE_HELD_HOUR_S && mStageRealS < LANEROUTE_HELD_HOUR_S + 1.0
            && mSimClock->GetClock().GetWarpIndex() < LANEROUTE_WARP_DAY)
        {
            SetWarpIndex(LANEROUTE_WARP_DAY);
        }
        SlowNearTarget(entry != nullptr ? entry->BodyB : INDEX_NONE);
        mHasArrived |= mOrderFrames > LANEROUTE_SKIP_FRAMES && mShips->GetAutopilotBodyIndex() == INDEX_NONE;
        if (!mHasArrived && simS < LANEROUTE_HELD_MAX_SIM_S && mCaseRealS < LANEROUTE_CASE_TIMEOUT_S)
        {
            return false;
        }
        Check(TEXT("held-closing-left"), mHasSeenHold && mHasSeenLeftForSun && mHasSeenLeftLine, FString::Printf(
            TEXT("hold %s, left for the Sun %s, HUD notice %s"), mHasSeenHold ? TEXT("seen") : TEXT("NOT SEEN"),
            mHasSeenLeftForSun ? TEXT("yes") : TEXT("NO"), mHasSeenLeftLine ? TEXT("shown") : TEXT("NOT SHOWN")));
        Check(TEXT("held-closing-sun"), mMinSunM >= SOL::NPC_LANE_SUN_CLEARANCE_RADII * SOL::SUN_RADIUS_M,
            FString::Printf(TEXT("closest approach %.2f Sun radii (zone 20), %s after %.1f days sim"),
            LaneRouteRadii(mMinSunM), mHasArrived ? TEXT("arrived") : TEXT("not arrived"), simS / 86400.0));
        return true;
    }
    }
}

//////////////////////////////////////////////////////////////////////////
// The Sun cases: the detour on every AUTO order, the override, the stop at the zone, a fast entry and the flips
bool FSOLLaneRouteSmoke::UpdateSun()
{
    const TCHAR* name = LaneRouteCaseName(static_cast<int32>(mCase));
    const double zoneM = SOL::NPC_LANE_SUN_CLEARANCE_RADII * SOL::SUN_RADIUS_M;
    const double edgeM = SOL::NPC_LANE_SUN_REOPEN_RADII * SOL::SUN_RADIUS_M;
    const double shipSunM = LANEROUTE_SHIP_SUN_DISTANCE_AU * SOL::AU_M;
    switch (mStage)
    {
    case 0:
        SetWarpIndex(0);
        SetOverride(mCase == ECase::SunOverride);
        if (mCase == ECase::SunChase)
        {
            PlaceAcrossSun(mMercury, shipSunM, LANEROUTE_PATH_OFFSET_RADII);
        }
        else if (mCase == ECase::SunTarget)
        {
            PlaceAcrossSun(mVenus, shipSunM, 0.0);
        }
        else if (mCase == ECase::FastEntry)
        {
            // On Venus's side, falling past the Sun inside the zone (its unpowered line passes at 15 radii)
            const FVector3d venusDir = BodyM(mVenus).GetSafeNormal();
            const FVector3d venusMps = BodyMps(mVenus);
            const FVector3d side = (venusMps - venusDir * venusMps.Dot(venusDir)).GetSafeNormal();
            const FVector3d shipM = venusDir * (LANEROUTE_FAST_START_RADII * SOL::SUN_RADIUS_M);
            const FVector3d heading = (side * (LANEROUTE_FAST_AIM_RADII * SOL::SUN_RADIUS_M) - shipM).GetSafeNormal();
            Place(shipM, heading * LANEROUTE_FAST_SPEED_MPS, heading);
        }
        else
        {
            PlaceAcrossSun(mVenus, shipSunM, LANEROUTE_PATH_OFFSET_RADII);
        }
        ++mStage;
        mStageRealS = 0.0;
        mStageFrames = 0;
        return false;
    case 1:
    {
        if (mStageRealS < (mCase == ECase::FastEntry ? 0.2 : 0.5))
        {
            return false;
        }
        bool bEngaged = false;
        if (mCase == ECase::SunChase)
        {
            FSOLChaseOrder order;
            order.BodyIndex = mMercury;
            bEngaged = mShips->EngageChase(order);
        }
        else
        {
            bEngaged = mShips->EngageAutopilot(mCase == ECase::SunTarget ? mSun : mVenus, mCase == ECase::SunWar);
        }
        SetWarpIndex(mCase == ECase::SunWar || mCase == ECase::SunChase ? LANEROUTE_WARP_DAY : LANEROUTE_WARP_HOUR);
        mCaseSimStartS = mSimClock->GetClock().GetSecondsSinceJ2000();
        const FVector3d shipM = mShips->GetState().PositionM;
        UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: %s engaged %s at %.2f Sun radii from the Sun, %.0f m/s, override %s"),
            name, bEngaged ? TEXT("yes") : TEXT("REFUSED"), LaneRouteRadii(shipM.Size()),
            mShips->GetState().VelocityMps.Size(), mShips->IsSunSafetyOverridden() ? TEXT("on") : TEXT("off"));
        ++mStage;
        mStageRealS = 0.0;
        mStageFrames = 0;
        return false;
    }
    default:
        break;
    }

    Observe();
    SlowNearTarget(mCase == ECase::SunChase ? mMercury : mCase == ECase::SunTarget ? INDEX_NONE : mVenus);
    const double simS = mSimClock->GetClock().GetSecondsSinceJ2000() - mCaseSimStartS;
    const bool bTimedOut = mCaseRealS > LANEROUTE_CASE_TIMEOUT_S || simS > LANEROUTE_SUN_MAX_SIM_S;

    // The flip case: the override on a few frames into the detour, then off again
    if (mCase == ECase::SunFlip)
    {
        if (mFlipFrame == INDEX_NONE && mHasSeenDetour && mOrderFrames > LANEROUTE_SKIP_FRAMES + LANEROUTE_FLIP_AFTER_FRAMES)
        {
            SetOverride(true);
            mFlipFrame = mOrderFrames;
            UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: sun-flip override ON at frame %d, %.2f Sun radii"), mOrderFrames,
                LaneRouteRadii(mShips->GetState().PositionM.Size()));
        }
        else if (mFlipFrame != INDEX_NONE && mOrderFrames == mFlipFrame + LANEROUTE_HUD_FRAMES)
        {
            mHasFlipOnCleared = !mShips->IsAutopilotSunDetour()
                && LaneRouteLineIs(mHud->GetLastSunSafetyLine(), SOLAutopilotOrder::SunSafetyLineText(true));
        }
        else if (mFlipFrame != INDEX_NONE && mOrderFrames == mFlipFrame + LANEROUTE_FLIP_HOLD_FRAMES)
        {
            SetOverride(false);
            UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: sun-flip override OFF at frame %d, %.2f Sun radii"),
                mOrderFrames, LaneRouteRadii(mShips->GetState().PositionM.Size()));
        }
        else if (mFlipFrame != INDEX_NONE && mOrderFrames > mFlipFrame + LANEROUTE_FLIP_HOLD_FRAMES
            && mOrderFrames <= mFlipFrame + 2 * LANEROUTE_FLIP_HOLD_FRAMES)
        {
            mHasFlipOffLatched |= mShips->IsAutopilotSunDetour();
        }
    }

    // A screenshot of the HUD's Sun line a few frames into the detour (or the override line)
    if (mOrderFrames == LANEROUTE_SKIP_FRAMES + 2 * LANEROUTE_HUD_FRAMES
        && (mCase == ECase::SunMove || mCase == ECase::SunOverride || mCase == ECase::SunTarget))
    {
        TakeShot(name);
    }

    // The end: arrival (the order gone), the chase on the tail, or the limit
    const bool bChaseCase = mCase == ECase::SunChase;
    const bool bOrder = bChaseCase ? mShips->IsChasing() : mShips->GetAutopilotBodyIndex() != INDEX_NONE;
    if (bChaseCase)
    {
        // The chase state every 120 order frames (a chase that never reaches the tail is diagnosed from these lines)
        const FSOLChaseStatus chase = mShips->GetChaseStatus();
        if (mOrderFrames % 120 == 0)
        {
            UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: sun-chase status at %.3f days, warp %d: tail %.4g km, target %.4g "
                "km, closing %.1f m/s, intercept %.0f s, holding %s, cooling %s, detour %s, real frame %.1f ms"),
                simS / 86400.0, mSimClock->GetClock().GetWarpIndex(), chase.AimDistanceM / 1000.0,
                chase.TargetDistanceM / 1000.0, chase.ClosingSpeedMps, chase.TimeToInterceptS, chase.bHolding
                ? TEXT("yes") : TEXT("no"), chase.bCooling ? TEXT("yes") : TEXT("no"), mShips->IsAutopilotSunDetour()
                ? TEXT("on") : TEXT("off"), FApp::GetDeltaTime() * 1000.0);
        }
        mStageFrames = chase.bHolding ? mStageFrames : 0;
        mHasArrived |= mStageFrames > LANEROUTE_CHASE_HOLD_FRAMES;
    }
    else
    {
        mHasArrived |= mOrderFrames > LANEROUTE_SKIP_FRAMES && !bOrder;
    }
    if (!mHasArrived && !bTimedOut && (bOrder || mOrderFrames <= LANEROUTE_SKIP_FRAMES))
    {
        return false;
    }

    // Each case's checks
    const double shipSunNowM = mShips->GetState().PositionM.Size();
    const FString sunDetail = FString::Printf(TEXT("closest approach %.2f Sun radii with the safety on (%.2f over the "
        "order), now %.2f; detour %s, HUD SUN DETOUR %s; %s after %.2f days sim"), LaneRouteRadii(mMinSunM),
        LaneRouteRadii(mMinSunAnyM), LaneRouteRadii(shipSunNowM), mHasSeenDetour ? TEXT("seen") : TEXT("not seen"),
        mHasSeenDetourLine ? TEXT("shown") : TEXT("not shown"), mHasArrived ? TEXT("arrived") : TEXT("NOT ARRIVED"),
        simS / 86400.0);
    switch (mCase)
    {
    case ECase::SunOverride:
        Check(TEXT("sun-override-straight"), !mHasSeenDetour && mMinSunAnyM > 0.0 && mMinSunAnyM < zoneM
            && mMinSunAnyM > SOL::SUN_RADIUS_M && mHasSeenSafetyOffLine && mHasArrived, FString::Printf(TEXT("%s; "
            "SUN SAFETY OFF with the order %s"), *sunDetail, mHasSeenSafetyOffLine ? TEXT("shown") : TEXT("NOT SHOWN")));
        break;
    case ECase::SunTarget:
        Check(TEXT("sun-target-stop"), mHasSeenStoppedLine && mHasArrived && mMinSunM >= zoneM
            && shipSunNowM >= zoneM && shipSunNowM <= edgeM + SOL::SUN_RADIUS_M, FString::Printf(TEXT("%s; STOPS AT "
            "SUN DANGER ZONE %s"), *sunDetail, mHasSeenStoppedLine ? TEXT("shown") : TEXT("NOT SHOWN")));
        break;
    case ECase::SunChase:
        Check(TEXT("sun-chase"), mHasSeenDetour && mMinSunM >= zoneM && mHasArrived, FString::Printf(TEXT("%s (on the "
            "tail %s)"), *sunDetail, mHasArrived ? TEXT("yes") : TEXT("NO")));
        break;
    case ECase::FastEntry:
        Check(TEXT("fast-entry-latch"), mHasSeenDetour && mHasArrived, sunDetail);
        // The accepted limitation (TECH_DEBT): a fast Sunward entry may dip just inside the 20-radii zone
        Check(TEXT("fast-entry-zone"), mMinSunM >= LANEROUTE_FAST_MIN_RADII * SOL::SUN_RADIUS_M
            && mMinSunAnyM >= LANEROUTE_FAST_FLOOR_RADII * SOL::SUN_RADIUS_M, FString::Printf(TEXT("%s (accepted bound "
            "%.0f radii, floor %.0f)"), *sunDetail, LANEROUTE_FAST_MIN_RADII, LANEROUTE_FAST_FLOOR_RADII));
        break;
    case ECase::SunFlip:
        Check(TEXT("sun-flip-on"), mFlipFrame != INDEX_NONE && mHasFlipOnCleared, FString::Printf(TEXT("override on at "
            "frame %d: detour cleared and SUN SAFETY OFF shown %s"), mFlipFrame, mHasFlipOnCleared ? TEXT("yes")
            : TEXT("NO")));
        Check(TEXT("sun-flip-off"), mHasFlipOffLatched && mHasArrived && mMinSunM >= zoneM, FString::Printf(TEXT(
            "override off again: detour re-latched %s; %s"), mHasFlipOffLatched ? TEXT("yes") : TEXT("NO"),
            *sunDetail));
        break;
    default:
        // Move To and War power across the Sun, the safety on
        Check(*FString::Printf(TEXT("%s-detour"), name), mHasSeenDetour && mHasSeenDetourLine && mMinSunM >= zoneM
            && mHasArrived, sunDetail);
        break;
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Observes this frame's Sun distance, detour, notices, plan, lane and epoch, and logs their changes
void FSOLLaneRouteSmoke::Observe()
{
    const TCHAR* name = LaneRouteCaseName(static_cast<int32>(mCase));
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    const bool bOrder = mShips->GetAutopilotBodyIndex() != INDEX_NONE || mShips->IsChasing();
    const double simDays = (mSimClock->GetClock().GetSecondsSinceJ2000() - mCaseSimStartS) / 86400.0;
    if (!bOrder)
    {
        return;
    }
    ++mOrderFrames;

    // The substep-swept closest approach (the flip case leaves out its override window and the frames after it)
    const bool bFlipWindow = mFlipFrame != INDEX_NONE && mOrderFrames >= mFlipFrame
        && mOrderFrames <= mFlipFrame + LANEROUTE_FLIP_HOLD_FRAMES + LANEROUTE_HUD_FRAMES;
    const double sunM = mShips->GetAutopilotSunMinDistanceM();
    if (mOrderFrames > LANEROUTE_SKIP_FRAMES && FMath::IsFinite(sunM) && sunM > 0.0)
    {
        mMinSunAnyM = mMinSunAnyM > 0.0 ? FMath::Min(mMinSunAnyM, sunM) : sunM;
        if (!mShips->IsSunSafetyOverridden() && !bFlipWindow)
        {
            mMinSunM = mMinSunM > 0.0 ? FMath::Min(mMinSunM, sunM) : sunM;
        }
    }

    // The Sun flags and the HUD's Sun line (drawn last frame)
    const bool bDetour = mShips->IsAutopilotSunDetour();
    if (bDetour != mLastDetour)
    {
        UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: %s detour %s at %.3f days, %.2f Sun radii"), name,
            bDetour ? TEXT("ON") : TEXT("off"), simDays, LaneRouteRadii(mShips->GetState().PositionM.Size()));
    }
    mLastDetour = bDetour;
    const bool bLate = mOrderFrames > LANEROUTE_SKIP_FRAMES;
    mHasSeenDetour |= bLate && bDetour;
    mHasSeenLeftForSun |= bLate && mShips->IsAutopilotLaneLeftForSun();
    const TCHAR* line = mHud->GetLastSunSafetyLine();
    mHasSeenDetourLine |= bLate && LaneRouteLineIs(line, SOLMenuText::AUTOPILOT_SUN_DETOUR);
    mHasSeenSafetyOffLine |= bLate && LaneRouteLineIs(line, SOLAutopilotOrder::SunSafetyLineText(true));
    mHasSeenStoppedLine |= bLate && LaneRouteLineIs(line, SOLMenuText::AUTOPILOT_STOPPED_AT_ZONE);
    mHasSeenLeftLine |= bLate && LaneRouteLineIs(line, SOLMenuText::AUTOPILOT_LANE_LEFT_FOR_SUN);

    // The lane mode, the plan (hops, serial) and the VIA line
    const ESOLLaneAutoMode mode = mShips->GetAutopilotLaneMode();
    const FSOLLaneRoutePlan plan = mShips->GetAutopilotLanePlan();
    const FSOLLaneAutoLeg leg = mShips->GetAutopilotLaneLeg();
    const bool bPlan = SOLLaneRoutePlan::HasPlan(plan);
    const TConstArrayView<FSOLLaneNode> nodes = mLaneTraffic->GetNetwork().GetConfig().Nodes;
    const auto nodeBody = [&nodes](const int32 node)
    {
        return nodes.IsValidIndex(node) ? nodes[node].BodyIndex : INDEX_NONE;
    };
    const auto bodyName = [&registry](const int32 body)
    {
        return body >= 0 && body < registry.Num() ? registry.GetName(body).ToString() : FString(TEXT("-"));
    };
    if (mode != mLastMode)
    {
        UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: %s lane mode %s -> %s at %.3f days (leg %s - %s)"), name,
            LaneRouteModeName(mLastMode), LaneRouteModeName(mode), simDays, *bodyName(leg.BodyA),
            *bodyName(leg.BodyB));
    }
    mLastMode = mode;
    const bool bLatched = (mode == ESOLLaneAutoMode::Approach || mode == ESOLLaneAutoMode::Hold) && leg.bFound;
    mHasSeenHold |= mode == ESOLLaneAutoMode::Hold;
    mHasSeenLaneLatch |= bLatched;

    // A plan hop ends when the current hop's lane changes or the plan goes: where the ship is then from the hop's end
    // body (in its radii), whether the hop held its lane and whether it was left early
    const FSOLLaneRoutePlanHop* const currentHop = bPlan
        ? &plan.Hops[FMath::Clamp(plan.CurrentHop, 0, plan.HopCount - 1)] : nullptr;
    const int32 hopFrom = currentHop != nullptr ? nodeBody(currentHop->FromNode) : INDEX_NONE;
    const int32 hopTo = currentHop != nullptr ? nodeBody(currentHop->ToNode) : INDEX_NONE;
    if (mHopToBody != INDEX_NONE && (hopFrom != mHopFromBody || hopTo != mHopToBody))
    {
        const double awayRadii = FVector3d::Dist(mShips->GetState().PositionM, BodyM(mHopToBody))
            / FMath::Max(registry.GetRadiusM(mHopToBody), 1.0);
        // A held hop leaves at its end ring (the transfer, often far beyond the at-node radius); a hop that never held
        // and ends away from its node was not flown
        const bool bAway = awayRadii > SOL::AUTO_LANE_PLAN_AT_NODE_RADII;
        // The plan's last hop is excluded from the held-lane assertion by decision (TECH_DEBT item 9), logged only
        // A hop left early is not away when it is left: the ship flies on to the hop's end node, where the plan
        // re-plans, and is then checked for reaching that node (further below)
        const bool bEarly = mWasHopLeftEarly && !mIsHopLast;
        mHopsEndedAway += bAway && !mHasHopHeld && !mIsHopLast && !bEarly ? 1 : 0;
        if (bEarly)
        {
            // The hop ended via FinishEarlyHop (the plan moved on or was re-made). The proximity sample is only
            // meaningful when one frame's travel is small against the end body's radius
            ++mEarlyHopsEnded;
            mEarlyHopsUnreached += mEarlyPendingBody != INDEX_NONE ? 1 : 0;
            const double stepRadii = mHasPrevShipPos ? FVector3d::Dist(mShips->GetState().PositionM, mPrevShipPosM)
                / FMath::Max(registry.GetRadiusM(mHopToBody), 1.0) : TNumericLimits<double>::Max();
            if (stepRadii < LANEROUTE_EARLY_SAMPLE_MAX_STEP_RADII)
            {
                mEarlyPendingBody = mHopToBody;
            }
            else
            {
                mEarlyPendingBody = INDEX_NONE;
                ++mEarlyProximitySkipped;
                UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: %s hop left early to %s ended via FinishEarlyHop, proximity "
                    "assertion skipped (one frame spans %.4g radii, limit %.0f)"), name, *bodyName(mHopToBody),
                    stepRadii, LANEROUTE_EARLY_SAMPLE_MAX_STEP_RADII);
            }
        }
        UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: %s %shop %s > %s %s at %.3f days: ship %.4g radii from %s (%s), held "
            "%s, left early %s"), name, mIsHopLast ? TEXT("LAST (excluded) ") : TEXT(""), *bodyName(mHopFromBody), *bodyName(mHopToBody), bPlan ? TEXT("ended")
            : TEXT("DROPPED with the plan"), simDays, awayRadii, *bodyName(mHopToBody), bAway ? TEXT("AWAY")
            : TEXT("at the node"), mHasHopHeld ? TEXT("yes") : TEXT("no"), mWasHopLeftEarly ? TEXT("yes")
            : TEXT("no"));
    }
    if (hopFrom != mHopFromBody || hopTo != mHopToBody)
    {
        mHasHopHeld = false;
    }
    // A hop left early: while a frame is small enough to sample it, the ship must come within the at-node radius of the
    // hop's end node (a frame too large to sample drops the assertion, logged)
    if (mEarlyPendingBody != INDEX_NONE && mHasPrevShipPos
        && FVector3d::Dist(mShips->GetState().PositionM, mPrevShipPosM)
        >= LANEROUTE_EARLY_SAMPLE_MAX_STEP_RADII * FMath::Max(registry.GetRadiusM(mEarlyPendingBody), 1.0))
    {
        UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: %s proximity assertion to %s dropped (frame too large to sample)"),
            name, *bodyName(mEarlyPendingBody));
        ++mEarlyProximitySkipped;
        mEarlyPendingBody = INDEX_NONE;
    }
    if (mEarlyPendingBody != INDEX_NONE && FVector3d::Dist(mShips->GetState().PositionM, BodyM(mEarlyPendingBody))
        <= SOL::AUTO_LANE_PLAN_AT_NODE_RADII * FMath::Max(registry.GetRadiusM(mEarlyPendingBody), 1.0))
    {
        ++mEarlyHopsReached;
        UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: %s hop left early reached %s within %.0f radii at %.3f days"), name,
            *bodyName(mEarlyPendingBody), SOL::AUTO_LANE_PLAN_AT_NODE_RADII, simDays);
        mEarlyPendingBody = INDEX_NONE;
    }
    mPrevShipPosM = mShips->GetState().PositionM;
    mHasPrevShipPos = true;
    mHopFromBody = hopFrom;
    mHopToBody = hopTo;
    mHasHopHeld |= bPlan && mode == ESOLLaneAutoMode::Hold;
    mWasHopLeftEarly = bPlan && plan.bHopLeftEarly;
    mIsHopLast = bPlan && plan.CurrentHop >= plan.HopCount - 1;
    if (bPlan)
    {
        mMaxHopCount = FMath::Max(mMaxHopCount, plan.HopCount);
        if (plan.Serial != mLastSerial || plan.CurrentHop != mLastHop)
        {
            FString hops;
            for (int32 hop = 0; hop < plan.HopCount; ++hop)
            {
                hops += FString::Printf(TEXT("%s%s>%s"), hop == 0 ? TEXT("") : TEXT(", "),
                    *bodyName(nodeBody(plan.Hops[hop].FromNode)), *bodyName(nodeBody(plan.Hops[hop].ToNode)));
            }
            UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: %s plan serial %u hop %d/%d epoch %lld%s at %.3f days: %s"),
                name, plan.Serial, plan.CurrentHop, plan.HopCount, plan.PlannedEpoch,
                plan.bHopLeftEarly ? TEXT(" (left early)") : TEXT(""), simDays, *hops);
            mReplans += (mLastSerial != 0 && plan.Serial != mLastSerial) ? 1 : 0;
        }
        mLastSerial = plan.Serial;
        mLastHop = plan.CurrentHop;

        // Only the current hop's lane may be latched mid-plan (the plan read on the same step as the leg)
        if (bLatched)
        {
            const FSOLLaneRoutePlanHop& hop = plan.Hops[FMath::Clamp(plan.CurrentHop, 0, plan.HopCount - 1)];
            const int32 fromBody = nodeBody(hop.FromNode);
            const int32 toBody = nodeBody(hop.ToNode);
            const bool bHopLane = (leg.BodyA == fromBody && leg.BodyB == toBody)
                || (leg.BodyA == toBody && leg.BodyB == fromBody);
            if (!bHopLane)
            {
                if (mWrongLaneFrames < 5)
                {
                    UE_LOG(LogSOL, Warning, TEXT("SmokeLaneRoute: %s lane %s - %s latched while hop %d is %s > %s"),
                        name, *bodyName(leg.BodyA), *bodyName(leg.BodyB), plan.CurrentHop, *bodyName(fromBody),
                        *bodyName(toBody));
                }
                ++mWrongLaneFrames;
            }
        }
    }
    if (bLatched && (leg.BodyA != mLastLegA || leg.BodyB != mLastLegB))
    {
        mHopChanges += mLastLegA != INDEX_NONE ? 1 : 0;
        mLastLegA = leg.BodyA;
        mLastLegB = leg.BodyB;
    }

    // A lane epoch while a plan hop is held (the re-plan of the later hops, Q14)
    const int64 epoch = mLaneTraffic->GetNetwork().GetCurrentEpoch();
    if (epoch != mLastEpoch)
    {
        mHeldEpochChanges += (mLastEpoch != 0 && bPlan && mode == ESOLLaneAutoMode::Hold) ? 1 : 0;
        mLastEpoch = epoch;
    }
    const int32 viaBody = mHud->GetLastViaBody();
    mViaFrames += viaBody != INDEX_NONE ? 1 : 0;
    if (viaBody != mLastViaBody)
    {
        UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: %s VIA %s at %.3f days"), name, *bodyName(viaBody), simDays);
    }
    mLastViaBody = viaBody;
}

//////////////////////////////////////////////////////////////////////////
// Drops the warp to 3600x once the order's last leg is near its target
void FSOLLaneRouteSmoke::SlowNearTarget(const int32 targetBody)
{
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();

    // The last metres at 1x: under warp the 10 s substeps leave a limit cycle near a heavy body's standoff (12 m at
    // 1.1 m/s at Saturn under 3600x), so the 10 m / 1 m/s arrival is never met
    // (a chase's tail hold uses the same arrival, so a chase within 1 km of its tail point goes to 1x too: at 2560x1440
    // the longer frames left it 12 m off the tail at 0.8 m/s near Mercury for good)
    const bool bFinalOrder = mShips->GetAutopilotViaBodyIndex() == INDEX_NONE
        && mShips->GetAutopilotBodyIndex() != INDEX_NONE
        && mShips->GetAutopilotGuidance().DistanceM < LANEROUTE_SLOW_FINAL_M;
    const bool bFinalChase = mShips->IsChasing() && mShips->GetChaseStatus().AimDistanceM < LANEROUTE_SLOW_FINAL_M;
    if (mSimClock->GetClock().GetWarpIndex() > 0 && mSimClock->GetClock().GetWarpIndex() <= LANEROUTE_WARP_HOUR
        && (bFinalOrder || bFinalChase) && !mShips->IsAutopilotSunDetour())
    {
        SetWarpIndex(0);
        UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: %s final approach: warp to 1x"),
            LaneRouteCaseName(static_cast<int32>(mCase)));
        return;
    }
    if (mSimClock->GetClock().GetWarpIndex() <= LANEROUTE_WARP_HOUR || mShips->GetAutopilotViaBodyIndex() != INDEX_NONE || targetBody < 0 || targetBody >= registry.Num()
        || FVector3d::Dist(mShips->GetState().PositionM, BodyM(targetBody))
        > LANEROUTE_SLOW_TARGET_RADII * registry.GetRadiusM(targetBody))
    {
        return;
    }
    SetWarpIndex(LANEROUTE_WARP_HOUR);
    UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: %s near %s: warp to 3600x"),
        LaneRouteCaseName(static_cast<int32>(mCase)), *registry.GetName(targetBody).ToString());
}

//////////////////////////////////////////////////////////////////////////
// Places the ship at rest in Earth's frame near Earth (inside the plan's at-node radius), nose away from the Sun
void FSOLLaneRouteSmoke::PlaceNearEarth()
{
    const FVector3d earthM = BodyM(mEarth);
    const FVector3d outward = earthM.GetSafeNormal();
    Place(earthM + outward * LANEROUTE_EARTH_OFFSET_M, BodyMps(mEarth), outward);
}

//////////////////////////////////////////////////////////////////////////
// Places the ship at rest across the Sun from a body: the Sun between them, the straight path offset along the
// body's motion by a number of Sun radii (so it misses the Sun itself but crosses the danger zone)
void FSOLLaneRouteSmoke::PlaceAcrossSun(const int32 body, const double shipSunDistanceM, const double offsetSunRadii)
{
    const FVector3d bodyDir = BodyM(body).GetSafeNormal();
    const FVector3d bodyMps = BodyMps(body);
    const FVector3d side = (bodyMps - bodyDir * bodyMps.Dot(bodyDir)).GetSafeNormal();
    const FVector3d shipM = -bodyDir * shipSunDistanceM + side * (offsetSunRadii * SOL::SUN_RADIUS_M);
    Place(shipM, FVector3d::ZeroVector, BodyM(body) - shipM);
}

//////////////////////////////////////////////////////////////////////////
// Places the ship with a velocity and a nose direction, no order (the chase and the autopilot disengaged)
void FSOLLaneRouteSmoke::Place(const FVector3d& positionM, const FVector3d& velocityMps, const FVector3d& noseDirection)
{
    mShips->DisengageChase();
    mShips->DisengageAutopilot();
    FSOLShipState state = mShips->GetState();
    state.PositionM = positionM;
    state.VelocityMps = velocityMps;
    state.AngularVelocityRadPerS = FVector3d::ZeroVector;
    state.Orientation = UE::Math::TRotationMatrix<double>::MakeFromX(noseDirection.GetSafeNormal()).ToQuat();
    mShips->SetState(state);
}

//////////////////////////////////////////////////////////////////////////
// Sets the Sun safety override to a state (through the subsystem's toggle)
void FSOLLaneRouteSmoke::SetOverride(const bool bOverridden) const
{
    if (mShips->IsSunSafetyOverridden() != bOverridden)
    {
        mShips->ToggleSunSafetyOverride();
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns a body's position in the Unreal-handed universe frame
FVector3d FSOLLaneRouteSmoke::BodyM(const int32 body) const
{
    return SOLRender::EclipticToUnreal(mRegistry->GetRegistry().GetPositionM(body));
}

//////////////////////////////////////////////////////////////////////////
// Returns a body's velocity in the Unreal-handed universe frame
FVector3d FSOLLaneRouteSmoke::BodyMps(const int32 body) const
{
    return SOLRender::EclipticToUnreal(mRegistry->GetRegistry().GetVelocityMps(body));
}

//////////////////////////////////////////////////////////////////////////
// Picks the open drivable lane between two registry bodies whose window closes soonest for the Sun (its A - B line
// within k_on of the Sun around the close); false if none closes in the schedule's horizon
bool FSOLLaneRouteSmoke::FindSunClosingLane()
{
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    const FSOLLaneNetworkSchedule& network = mLaneTraffic->GetNetwork();
    const TConstArrayView<FSOLLaneTrafficEntry> lanes = mLaneTraffic->GetLanes();
    const double nowS = mSimClock->GetClock().GetSecondsSinceJ2000();
    const double edgeM = SOL::NPC_LANE_SUN_REOPEN_RADII * SOL::SUN_RADIUS_M;
    mClosingEntry = INDEX_NONE;
    for (int32 lane = 0; lane < lanes.Num(); ++lane)
    {
        const FSOLLaneTrafficEntry& entry = lanes[lane];
        double openS = 0.0;
        double closeS = 0.0;
        if (entry.Slot == INDEX_NONE || !entry.bOpenNow || !entry.Profile.bDrivable || entry.BodyA < 0
            || entry.BodyB < 0 || entry.BodyA >= registry.Num() || entry.BodyB >= registry.Num()
            || !network.GetSlotWindow(entry.Slot, openS, closeS) || !FMath::IsFinite(closeS)
            || closeS < nowS + LANEROUTE_CLOSE_MIN_S || closeS > nowS + LANEROUTE_CLOSE_MAX_S)
        {
            continue;
        }
        // A Sun closure: the A - B line near the Sun around the close (a Gone closure finishes the hop, by design)
        double sunM = TNumericLimits<double>::Max();
        for (int32 step = -1; step <= 1; ++step)
        {
            const double timeS = closeS + step * SOL::NPC_LANE_NETWORK_EPOCH_S;
            sunM = FMath::Min(sunM, SOLLaneNetwork::SunSegmentDistanceM(registry.ComputePositionAtM(entry.BodyA,
                timeS), registry.ComputePositionAtM(entry.BodyB, timeS)));
        }
        if (sunM < edgeM && (mClosingEntry == INDEX_NONE || closeS < mClosingCloseS))
        {
            mClosingEntry = lane;
            mClosingCloseS = closeS;
        }
    }
    UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: Sun-closing lane %d (%s - %s), closes in %.2f days"), mClosingEntry,
        mClosingEntry != INDEX_NONE ? *registry.GetName(lanes[mClosingEntry].BodyA).ToString() : TEXT("-"),
        mClosingEntry != INDEX_NONE ? *registry.GetName(lanes[mClosingEntry].BodyB).ToString() : TEXT("-"),
        mClosingEntry != INDEX_NONE ? (mClosingCloseS - nowS) / 86400.0 : -1.0);
    return mClosingEntry != INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Sets the time warp to a factor index (0 = 1x)
void FSOLLaneRouteSmoke::SetWarpIndex(const int32 index) const
{
    mSimClock->ResetWarp();
    for (int32 step = 0; step < index; ++step)
    {
        mSimClock->StepWarpUp();
    }
}

//////////////////////////////////////////////////////////////////////////
// Logs the SUMMARY line, restores the warp, overrides and space dust and takes the closing screenshot (which quits)
void FSOLLaneRouteSmoke::Finish(ASOLShipPawn& pawn)
{
    if (mShips.IsValid())
    {
        mShips->DisengageChase();
        mShips->DisengageAutopilot();
        SetOverride(false);
    }
    if (mSimClock.IsValid())
    {
        mSimClock->ResetWarp();
    }
    LaneRouteSetCvar(SOL::SpaceDust::CVAR_ENABLED, 1);
    UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute SUMMARY: complete after %.1f s, %d/%d checks run passed%s"), mTotalTimeS,
        mChecksPassed, mChecksRun, mChecksPassed == mChecksRun && mChecksRun > 0 ? TEXT("") : TEXT(" (FAILURES)"));
    if (ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>())
    {
        gameMode->TakeSmokeScreenshot();
    }
}

//////////////////////////////////////////////////////////////////////////
// Requests a screenshot with the HUD named LaneRoute_<width>x<height>_<name>.png
void FSOLLaneRouteSmoke::TakeShot(const TCHAR* name) const
{
    FVector2D size(0.0, 0.0);
    if (mHud.IsValid() && mHud->GetWorld()->GetGameViewport() != nullptr)
    {
        mHud->GetWorld()->GetGameViewport()->GetViewportSize(size);
    }
    const FString path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / FString::Printf(
        TEXT("LaneRoute_%dx%d_%s.png"), FMath::RoundToInt32(size.X), FMath::RoundToInt32(size.Y), name));
    FScreenshotRequest::RequestScreenshot(path, true, false);
    UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute: screenshot %s"), *path);
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLLaneRouteSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SmokeLaneRoute CHECK %-24s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"),
        *detail);
}

//////////////////////////////////////////////////////////////////////////
// Resets the per-case observations
void FSOLLaneRouteSmoke::ResetObservations()
{
    mOrderFrames = 0;
    mMinSunM = 0.0;
    mMinSunAnyM = 0.0;
    mHasSeenDetour = false;
    mHasSeenDetourLine = false;
    mHasSeenSafetyOffLine = false;
    mHasSeenStoppedLine = false;
    mHasSeenLeftLine = false;
    mHasSeenLeftForSun = false;
    mHasSeenHold = false;
    mHasSeenLaneLatch = false;
    mHasArrived = false;
    mWrongLaneFrames = 0;
    mHopChanges = 0;
    mReplans = 0;
    mHeldEpochChanges = 0;
    mMaxHopCount = 0;
    mViaFrames = 0;
    mLastViaBody = INDEX_NONE;
    mLastLegA = INDEX_NONE;
    mLastLegB = INDEX_NONE;
    mLastHop = INDEX_NONE;
    mHopFromBody = INDEX_NONE;
    mHopToBody = INDEX_NONE;
    mHasHopHeld = false;
    mWasHopLeftEarly = false;
    mIsHopLast = false;
    mHopsEndedAway = 0;
    mEarlyPendingBody = INDEX_NONE;
    mEarlyHopsReached = 0;
    mEarlyHopsUnreached = 0;
    mEarlyHopsEnded = 0;
    mEarlyProximitySkipped = 0;
    mHasPrevShipPos = false;
    mLastSerial = 0;
    mLastEpoch = 0;
    mLastMode = ESOLLaneAutoMode::None;
    mLastDetour = false;
    mFlipFrame = INDEX_NONE;
    mHasFlipOnCleared = false;
    mHasFlipOffLatched = false;
    mIsShotTaken = false;
}
