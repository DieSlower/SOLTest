/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Interaction/SOLInteractSmoke.h"

#include "Combat/SOLCombatSubsystem.h"
#include "Interaction/SOLInteractionSubsystem.h"
#include "Map/SOLJumpSubsystem.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "UI/SOLActionListWidget.h"
#include "UI/SOLFlightHud.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
    // The script's steps, in order
    enum class EInteractStep : int32
    {
        Setup,              // Let the HUD draw icons; pick a body icon
        Hover,              // Cursor on the icon
        PressOpen,          // Fire press on it: the radial menu opens
        DragTarget,         // Pointer onto the Target sector
        ReleaseTarget,      // Release: targeted
        CancelCentre,       // Press, release in the centre: cancelled
        EscOpen,            // Press (menu open for the Esc)
        EscCancel,          // Esc: cancelled, no pause
        FireOffIcon,        // Fire away from every icon: the trigger holds
        FireRelease,        // ... released
        Info,               // Radial Info: the card shows
        MoveTo,             // Radial Move To: autopilot to the body
        Follow,             // Radial Follow / Orbit: station-keeping order
        Match,              // Radial Match speed: frame locked
        DropTarget,         // G: a target ahead
        Attack,             // Radial Attack on the dropped target
        JumpTo,             // Radial Jump To on the body
        JumpWait,           // The warp runs and arrives
        MapOpen,            // Shift+M
        ListOpen,           // Right-click list on the body
        ListEsc,            // Down, then Esc: closed, the map stays open
        ListMove,           // Reopened, Move To confirmed
        MapClose,
        Quit,
        Count,
    };

    // Step durations (real seconds) and the minimum frames per step
    constexpr double INTERACTSMOKE_SETUP_S = 3.0;
    constexpr double INTERACTSMOKE_STEP_S = 0.5;
    constexpr double INTERACTSMOKE_SHOT_S = 1.2;
    constexpr double INTERACTSMOKE_QUIT_S = 3.0;
    constexpr int32 INTERACTSMOKE_MIN_FRAMES = 5;
    constexpr double INTERACTSMOKE_ARRIVAL_TOLERANCE = 0.05;   // Jump arrival distance, relative
}

//////////////////////////////////////////////////////////////////////////
// Caches the controller, HUD and interaction subsystem and starts the first step
void FSOLInteractSmoke::Start(ASOLShipPawn& pawn)
{
    mController = pawn.GetController<APlayerController>();
    mHud = mController.IsValid() ? mController->GetHUD<ASOLFlightHud>() : nullptr;
    mInteraction = pawn.GetWorld()->GetSubsystem<USOLInteractionSubsystem>();
    pawn.SetFlightCursorScripted(true);
    UE_LOG(LogSOL, Log, TEXT("SmokeInteract: scripted interaction input starting"));
    mStep = 0;
    EnterStep(mStep, pawn);
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script; returns true once it has finished
bool FSOLInteractSmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    if (mStep >= static_cast<int32>(EInteractStep::Count))
    {
        return true;
    }
    if (!mController.IsValid() || !mInteraction.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeInteract: controller or interaction subsystem missing; aborting"));
        mStep = static_cast<int32>(EInteractStep::Count);
        return true;
    }
    if (!mHud.IsValid())
    {
        mHud = mController->GetHUD<ASOLFlightHud>();
    }

    // Each step lasts its duration and at least a few frames, then checks and moves on
    mStepTimeS += realDeltaSeconds;
    ++mStepFrames;
    const EInteractStep step = static_cast<EInteractStep>(mStep);
    const double durationS = step == EInteractStep::Setup ? INTERACTSMOKE_SETUP_S
        : step == EInteractStep::JumpWait ? SOL::WARP_DURATION_S + 1.5
        : step == EInteractStep::DragTarget || step == EInteractStep::Info || step == EInteractStep::ListOpen
            || step == EInteractStep::Attack || step == EInteractStep::MapOpen ? INTERACTSMOKE_SHOT_S
        : step == EInteractStep::Quit ? INTERACTSMOKE_QUIT_S : INTERACTSMOKE_STEP_S;
    if (mStepTimeS < durationS || mStepFrames < INTERACTSMOKE_MIN_FRAMES)
    {
        return false;
    }
    ExitStep(mStep, pawn);
    ++mStep;
    mStepTimeS = 0.0;
    mStepFrames = 0;
    if (mStep >= static_cast<int32>(EInteractStep::Count))
    {
        return true;
    }
    EnterStep(mStep, pawn);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Finds an icon hit of an object kind (bodies: not the Sun, preferring one that is not the anchor)
bool FSOLInteractSmoke::FindHit(const ESOLObjectKind kind, const int32 anchorBody, FSOLObjectRef& outObject,
    FVector2D& outCenterPx) const
{
    bool bFound = false;
    for (const FSOLIconHit& hit : mInteraction->GetIconHits())
    {
        if (hit.Object.Kind != kind || (kind == ESOLObjectKind::Body && hit.Object.Index == 0))
        {
            continue;
        }
        const bool bBetterBody = kind == ESOLObjectKind::Body && outObject.Index == anchorBody
            && hit.Object.Index != anchorBody;
        if (!bFound || bBetterBody)
        {
            outObject = hit.Object;
            outCenterPx = hit.CenterPx;
            bFound = true;
        }
    }
    return bFound;
}

//////////////////////////////////////////////////////////////////////////
// Returns the current screen centre of an object's icon (the last one found when it is not drawn now)
FVector2D FSOLInteractSmoke::GetObjectIconPx(const FSOLObjectRef& object) const
{
    for (const FSOLIconHit& hit : mInteraction->GetIconHits())
    {
        if (hit.Object == object)
        {
            return hit.CenterPx;
        }
    }
    return object == mTarget ? mTargetPx : mBodyPx;
}

//////////////////////////////////////////////////////////////////////////
// Presses Fire on the object's icon (opens the radial menu)
void FSOLInteractSmoke::PressOnIcon(ASOLShipPawn& pawn, const FSOLObjectRef& object)
{
    pawn.HandleFlightCursorMoved(GetObjectIconPx(object));
    mInteraction->UpdateHover(pawn.GetFlightCursorPx(), true);
    pawn.HandleFire(true);
}

//////////////////////////////////////////////////////////////////////////
// Moves the cursor onto an action's sector of the open radial menu (the middle of the sector ring)
bool FSOLInteractSmoke::DragToAction(ASOLShipPawn& pawn, const ESOLAction action)
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
            return true;
        }
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's action at its start
void FSOLInteractSmoke::EnterStep(const int32 step, ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    USOLShipSubsystem* ships = world->GetSubsystem<USOLShipSubsystem>();
    mExecutedBefore = mInteraction->GetExecutedCount();
    mCancelsBefore = mInteraction->GetRadialCancelCount();

    // Each radial action step: press on the icon now; the drag and the release follow in the same frame (the pawn's
    // tick moves the highlight before the HUD draws)
    const auto radialAction = [this, &pawn](const FSOLObjectRef& object, const ESOLAction action)
    {
        PressOnIcon(pawn, object);
        DragToAction(pawn, action);
        mInteraction->MovePointer(pawn.GetFlightCursorPx());
        pawn.HandleFire(false);
    };
    switch (static_cast<EInteractStep>(step))
    {
    case EInteractStep::Hover:
        pawn.HandleFlightCursorMoved(GetObjectIconPx(mBody));
        break;
    case EInteractStep::PressOpen:
        PressOnIcon(pawn, mBody);
        if (FParse::Param(FCommandLine::Get(), SOL::CommandLine::SMOKE_INTERACT_PROFILE) && GEngine != nullptr)
        {
            // Cost with the radial menu open: the game-thread stats over the next frames, then one GPU frame
            GEngine->Exec(world, TEXT("stat dumpave -num=60 -ms=0"));
        }
        break;
    case EInteractStep::EscOpen:
        PressOnIcon(pawn, mBody);
        break;
    case EInteractStep::DragTarget:
        DragToAction(pawn, ESOLAction::Target);
        TakeUiShot(TEXT("Radial"));
        if (FParse::Param(FCommandLine::Get(), SOL::CommandLine::SMOKE_INTERACT_PROFILE) && GEngine != nullptr)
        {
            GEngine->Exec(world, TEXT("ProfileGPU"));
        }
        break;
    case EInteractStep::ReleaseTarget:
        pawn.HandleFire(false);
        break;
    case EInteractStep::CancelCentre:
        PressOnIcon(pawn, mBody);
        pawn.HandleFlightCursorMoved(mInteraction->GetRadial().CenterPx);
        mInteraction->MovePointer(pawn.GetFlightCursorPx());
        pawn.HandleFire(false);
        break;
    case EInteractStep::EscCancel:
        pawn.HandlePause();
        break;
    case EInteractStep::FireOffIcon:
    {
        // A point away from every icon (the bottom middle, above the edge)
        int32 width = 0;
        int32 height = 0;
        mController->GetViewportSize(width, height);
        pawn.HandleFlightCursorMoved(FVector2D(width * 0.5, height * 0.9));
        pawn.HandleFire(true);
        break;
    }
    case EInteractStep::FireRelease:
        pawn.HandleFire(false);
        break;
    case EInteractStep::Info:
        radialAction(mBody, ESOLAction::Info);
        TakeUiShot(TEXT("Info"));
        break;
    case EInteractStep::MoveTo:
        radialAction(mBody, ESOLAction::MoveTo);
        break;
    case EInteractStep::Follow:
        radialAction(mBody, ESOLAction::Follow);
        break;
    case EInteractStep::Match:
        radialAction(mBody, ESOLAction::MatchSpeed);
        break;
    case EInteractStep::DropTarget:
        if (ships != nullptr)
        {
            // At rest in the reference frame, so the target (dropped at rest in it) stays ahead and on screen
            ships->DisengageAutopilot();
            FSOLShipState state = ships->GetState();
            state.VelocityMps = ships->GetReferenceVelocityMps();
            ships->SetState(state);
        }
        pawn.HandleDropTarget();
        break;
    case EInteractStep::Attack:
        if (FindHit(ESOLObjectKind::DroppedTarget, INDEX_NONE, mTarget, mTargetPx))
        {
            radialAction(mTarget, ESOLAction::Attack);
        }
        TakeUiShot(TEXT("Attack"));
        break;
    case EInteractStep::JumpTo:
        mJumpsBefore = world->GetSubsystem<USOLJumpSubsystem>()->GetCompletedJumpCount();
        radialAction(mBody, ESOLAction::JumpTo);
        break;
    case EInteractStep::MapOpen:
        pawn.HandleOpenMap(false);
        break;
    case EInteractStep::ListOpen:
    case EInteractStep::ListMove:
    {
        int32 width = 0;
        int32 height = 0;
        mController->GetViewportSize(width, height);
        pawn.OpenActionList(mBody, FVector2D(width * 0.5, height * 0.5));
        if (static_cast<EInteractStep>(step) == EInteractStep::ListOpen)
        {
            TakeUiShot(TEXT("List"));
        }
        else if (USOLActionListWidget* list = pawn.GetActionList())
        {
            const FSOLActionList& actions = list->GetActions();
            for (int32 row = 0; row < actions.Num; ++row)
            {
                if (actions.Entries[row].Action == ESOLAction::MoveTo)
                {
                    list->ConfirmRow(row);
                }
            }
        }
        break;
    }
    case EInteractStep::ListEsc:
        SlateKey(EKeys::Down);
        SlateKey(EKeys::Escape);
        break;
    case EInteractStep::MapClose:
        pawn.HandleCloseMap();
        break;
    case EInteractStep::Quit:
        UE_LOG(LogSOL, Log, TEXT("SmokeInteract: %d/%d checks passed"), mChecksPassed, mChecksRun);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's checks at its end
void FSOLInteractSmoke::ExitStep(const int32 step, ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    const USOLShipSubsystem* ships = world->GetSubsystem<USOLShipSubsystem>();
    const USOLTargetingSubsystem* targeting = world->GetSubsystem<USOLTargetingSubsystem>();
    const USOLCombatSubsystem* combat = world->GetSubsystem<USOLCombatSubsystem>();
    const USOLJumpSubsystem* jump = world->GetSubsystem<USOLJumpSubsystem>();
    const USOLMenuSubsystem* menu = world->GetSubsystem<USOLMenuSubsystem>();
    const ASOLFlightHud* hud = mHud.Get();
    const int32 executed = mInteraction->GetExecutedCount() - mExecutedBefore;
    const int32 cancels = mInteraction->GetRadialCancelCount() - mCancelsBefore;
    const ESOLAction last = mInteraction->GetLastExecutedAction();
    const TCHAR* lastLabel = SOLActionTable::ActionLabel(last);
    switch (static_cast<EInteractStep>(step))
    {
    case EInteractStep::Setup:
    {
        const USOLAnchorSubsystem* anchor = world->GetSubsystem<USOLAnchorSubsystem>();
        const bool bFound = FindHit(ESOLObjectKind::Body, anchor != nullptr ? anchor->GetAnchorIndex() : INDEX_NONE,
            mBody, mBodyPx);
        Check(TEXT("a-hits"), bFound && mInteraction->GetIconHits().Num() > 0, FString::Printf(TEXT("%d icon hits; "
            "body %d at (%.0f, %.0f)"), mInteraction->GetIconHits().Num(), mBody.Index, mBodyPx.X, mBodyPx.Y));
        TakeUiShot(TEXT("Start"));
        break;
    }
    case EInteractStep::Hover:
        Check(TEXT("b-hover"), mInteraction->GetHovered() == mBody, FString::Printf(TEXT("hovered %d:%d"),
            static_cast<int32>(mInteraction->GetHovered().Kind), mInteraction->GetHovered().Index));
        break;
    case EInteractStep::PressOpen:
        Check(TEXT("c-press-opens"), mInteraction->IsRadialOpen() && combat != nullptr
            && !combat->IsPlayerTriggerHeld() && hud != nullptr && hud->GetLastRadialSectorCount() > 0,
            FString::Printf(TEXT("radial %s with %d sectors drawn, trigger %s"), mInteraction->IsRadialOpen()
            ? TEXT("open") : TEXT("closed"), hud != nullptr ? hud->GetLastRadialSectorCount() : 0,
            combat != nullptr && combat->IsPlayerTriggerHeld() ? TEXT("HELD") : TEXT("released")));
        break;
    case EInteractStep::DragTarget:
        Check(TEXT("d-drag"), mInteraction->GetRadial().Highlighted == 0, FString::Printf(TEXT("highlighted "
            "sector %d (Target is 0)"), mInteraction->GetRadial().Highlighted));
        break;
    case EInteractStep::ReleaseTarget:
        Check(TEXT("e-target"), !mInteraction->IsRadialOpen() && targeting != nullptr
            && targeting->GetSelectedIndex() == mBody.Index && last == ESOLAction::Target,
            FString::Printf(TEXT("release: menu %s, selected %d (body %d), last %s"), mInteraction->IsRadialOpen()
            ? TEXT("open") : TEXT("closed"), targeting != nullptr ? targeting->GetSelectedIndex() : -1, mBody.Index,
            lastLabel));
        break;
    case EInteractStep::CancelCentre:
        Check(TEXT("f-cancel-centre"), !mInteraction->IsRadialOpen() && cancels == 1 && executed == 0,
            FString::Printf(TEXT("release in the centre: %d cancel(s), %d action(s)"), cancels, executed));
        break;
    case EInteractStep::EscCancel:
        Check(TEXT("g-esc-cancel"), !mInteraction->IsRadialOpen() && cancels >= 1 && executed == 0
            && (menu == nullptr || !menu->IsMenuShowing()), FString::Printf(TEXT("Esc: %d cancel(s), %d action(s), "
            "pause menu %s"), cancels, executed, menu != nullptr && menu->IsMenuShowing() ? TEXT("SHOWN")
            : TEXT("not shown")));
        break;
    case EInteractStep::FireOffIcon:
        Check(TEXT("h-fire-off-icon"), !mInteraction->IsRadialOpen() && combat != nullptr
            && combat->IsPlayerTriggerHeld(), FString::Printf(TEXT("Fire away from icons: menu %s, trigger %s"),
            mInteraction->IsRadialOpen() ? TEXT("OPEN") : TEXT("closed"), combat != nullptr
            && combat->IsPlayerTriggerHeld() ? TEXT("held") : TEXT("NOT held")));
        break;
    case EInteractStep::Info:
        Check(TEXT("i-info"), mInteraction->GetInfoObject() == mBody && hud != nullptr
            && hud->GetLastInfoCardLineCount() >= 3, FString::Printf(TEXT("info card for %d, %d lines"),
            mInteraction->GetInfoObject().Index, hud != nullptr ? hud->GetLastInfoCardLineCount() : 0));
        break;
    case EInteractStep::MoveTo:
        Check(TEXT("j-move-to"), ships != nullptr && ships->GetFlightMode() == ESOLFlightMode::Auto
            && ships->GetAutopilotBodyIndex() == mBody.Index && !ships->IsAutopilotPointOrder(),
            FString::Printf(TEXT("last %s; autopilot body %d (want %d), point order %s"), lastLabel,
            ships != nullptr ? ships->GetAutopilotBodyIndex() : -1, mBody.Index, ships != nullptr
            && ships->IsAutopilotPointOrder() ? TEXT("yes") : TEXT("no")));
        break;
    case EInteractStep::Follow:
        Check(TEXT("k-follow"), ships != nullptr && ships->GetFlightMode() == ESOLFlightMode::Auto
            && ships->GetAutopilotBodyIndex() == mBody.Index && ships->IsAutopilotPointOrder() && last
            == ESOLAction::Follow, FString::Printf(TEXT("last %s; autopilot body %d, point order %s"), lastLabel,
            ships != nullptr ? ships->GetAutopilotBodyIndex() : -1, ships != nullptr && ships->IsAutopilotPointOrder()
            ? TEXT("yes") : TEXT("no")));
        break;
    case EInteractStep::Match:
        Check(TEXT("l-match"), targeting != nullptr && targeting->GetLockedIndex() == mBody.Index,
            FString::Printf(TEXT("last %s; frame locked to %d"), lastLabel, targeting != nullptr
            ? targeting->GetLockedIndex() : -1));
        break;
    case EInteractStep::Attack:
    {
        const FSOLTargetInfo* selected = targeting != nullptr ? targeting->GetSelectedTarget() : nullptr;
        Check(TEXT("m-attack"), mTarget.IsValid() && last == ESOLAction::Attack && selected != nullptr
            && selected->Name.ToString().StartsWith(TEXT("Target")) && mInteraction->GetHintAction()
            == ESOLAction::Attack, FString::Printf(TEXT("dropped target %d; last %s; selected %s; hint %s"),
            mTarget.Index, lastLabel, selected != nullptr ? *selected->Name.ToString() : TEXT("none"),
            mInteraction->GetHintAction() == ESOLAction::Attack ? TEXT("shown") : TEXT("NOT shown")));
        break;
    }
    case EInteractStep::JumpTo:
        Check(TEXT("n-jump-start"), jump != nullptr && jump->IsWarping() && last == ESOLAction::JumpTo,
            FString::Printf(TEXT("last %s; warp %s"), lastLabel, jump != nullptr && jump->IsWarping()
            ? TEXT("running") : TEXT("NOT running")));
        break;
    case EInteractStep::JumpWait:
    {
        const USOLBodyRegistrySubsystem* bodies = world->GetSubsystem<USOLBodyRegistrySubsystem>();
        double distanceM = 0.0;
        double wantM = 0.0;
        if (bodies != nullptr && ships != nullptr)
        {
            const FSOLBodyRegistry& registry = bodies->GetRegistry();
            distanceM = FVector3d::Dist(ships->GetUniversePositionM(), registry.GetPositionM(mBody.Index));
            wantM = SOLActionOrders::JumpArrivalDistanceM(registry.GetRadiusM(mBody.Index));
        }
        Check(TEXT("o-jump-arrive"), jump != nullptr && jump->GetCompletedJumpCount() == mJumpsBefore + 1
            && FMath::Abs(distanceM - wantM) <= INTERACTSMOKE_ARRIVAL_TOLERANCE * wantM,
            FString::Printf(TEXT("jumps %d -> %d; %.0f km from the body (want %.0f km)"), mJumpsBefore,
            jump != nullptr ? jump->GetCompletedJumpCount() : -1, distanceM / 1000.0, wantM / 1000.0));
        break;
    }
    case EInteractStep::ListOpen:
    {
        const USOLActionListWidget* list = pawn.GetActionList();
        const int32 rows = list != nullptr ? list->GetActions().Num : 0;
        Check(TEXT("p-list-open"), pawn.IsMapOpen() && pawn.IsActionListOpen()
            && rows == SOLActionTable::RowCount(mInteraction->GetObjectType(mBody)), FString::Printf(TEXT("map %s, "
            "list %s with %d rows"), pawn.IsMapOpen() ? TEXT("open") : TEXT("closed"), pawn.IsActionListOpen()
            ? TEXT("open") : TEXT("closed"), rows));
        break;
    }
    case EInteractStep::ListEsc:
        Check(TEXT("q-list-esc"), pawn.IsMapOpen() && !pawn.IsActionListOpen(), FString::Printf(TEXT("Down, Esc: "
            "list %s, map %s"), pawn.IsActionListOpen() ? TEXT("OPEN") : TEXT("closed"), pawn.IsMapOpen()
            ? TEXT("open") : TEXT("CLOSED")));
        break;
    case EInteractStep::ListMove:
        Check(TEXT("r-list-move"), pawn.IsMapOpen() && !pawn.IsActionListOpen() && ships != nullptr
            && ships->GetFlightMode() == ESOLFlightMode::Auto && ships->GetAutopilotBodyIndex() == mBody.Index,
            FString::Printf(TEXT("Move To from the list: last %s, autopilot body %d, list %s, map %s"), lastLabel,
            ships != nullptr ? ships->GetAutopilotBodyIndex() : -1, pawn.IsActionListOpen() ? TEXT("OPEN")
            : TEXT("closed"), pawn.IsMapOpen() ? TEXT("open") : TEXT("CLOSED")));
        break;
    case EInteractStep::Quit:
        pawn.SetFlightCursorScripted(false);
        UKismetSystemLibrary::QuitGame(world, nullptr, EQuitPreference::Quit, false);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Sends a key down and up through Slate (the list menu has the focus)
void FSOLInteractSmoke::SlateKey(const FKey& key)
{
    if (FSlateApplication::IsInitialized())
    {
        FSlateApplication& slate = FSlateApplication::Get();
        const FKeyEvent keyEvent(key, FModifierKeysState(), slate.GetUserIndexForKeyboard(), false, 0, 0);
        slate.ProcessKeyDownEvent(keyEvent);
        slate.ProcessKeyUpEvent(keyEvent);
    }
}

//////////////////////////////////////////////////////////////////////////
// Requests a window screenshot with the UI to Saved/Screenshots
void FSOLInteractSmoke::TakeUiShot(const TCHAR* name)
{
    const FString path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir()
        / FString::Printf(TEXT("InteractSmoke_%s.png"), name));
    FScreenshotRequest::RequestScreenshot(path, true, false);
    UE_LOG(LogSOL, Log, TEXT("SmokeInteract: screenshot with UI %s"), *path);
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLInteractSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SmokeInteract CHECK %-16s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"),
        *detail);
}
