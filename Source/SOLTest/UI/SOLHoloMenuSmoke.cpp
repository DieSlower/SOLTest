/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHoloMenuSmoke.h"

#include "Interaction/SOLInteractionSubsystem.h"
#include "Map/SOLJumpSubsystem.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "Ship/SOLShipPawn.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "UI/SOLFlightHud.h"
#include "UI/SOLHoloFrame.h"
#include "UI/SOLHoloFrameWidget.h"
#include "UI/SOLHoloMenuSubsystem.h"
#include "UI/SOLHologramCursorSubsystem.h"
#include "Universe/SOLAnchorSubsystem.h"

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"

namespace
{
    // The script's steps, in order
    enum class EHoloStep : int32
    {
        Setup,          // The world settles; the subsystem, its material, the menu layer and the HUD layer are checked
        Cues,           // One request of each cue, then a repeat inside the rate limit
        FrameAdd,       // The frame preview is put on the viewport (built, still Closed)
        FrameOpen,      // BeginOpen at strength 1: the open cue, the mid-open draw-in, then the full frame
        FrameHover,     // A hovered row: the hover cue and mark, the reused build equal to a fresh one
        FrameFlat,      // Strength 0: reopened whole and flat at once (D18), the open cue still requested (D21)
        FrameStrong,    // Strength 2
        Cursor,         // The cursor's strength follows HoloMenuEffects, not the HUD slider (D19, step 10)
        FrameClose,     // BeginClose at strength 1: Closing, then Closed with nothing drawn; the preview removed
        RadialOpen,     // Radial (step 5): Fire on a body's icon: the open cue, the rim arcs mid draw-in, then Open
        RadialHover,    // Drag onto Target, then Info: two hover cues, the hover glow on the highlighted sector
        RadialSelect,   // Release on Info: the select cue, the close drawn from the cached state, then nothing drawn
        RadialFlat,     // Strength 0 (step 9): open whole at once, flat arcs and no glow, the hover cue still asked
        RadialFlatSelect,   // Release on Info at strength 0: the action runs, the select cue, closed at once
        RadialStrong,   // Strength 2: wide glow pads and the hover glow
        RadialCancel,   // Release in the centre: cancelled, no select cue
        RadialDisabled, // Release on a disabled sector (Communications): nothing runs, no select cue (SDD 5)
        RadialJump,     // Release on Jump To: the select cue on the release frame, not after the warp (42d review)
        Stubs,         // The section 12 checks later parts fill in
        Quit,           // The SUMMARY line, then quit
        Count,
    };

    constexpr int32 HOLOSMOKE_MIN_FRAMES = 3;
    constexpr int32 HOLOSMOKE_SHOT_FRAMES = 3;                 // Frames a step waits after its screenshot request
    constexpr double HOLOSMOKE_SETUP_S = 3.0;
    constexpr double HOLOSMOKE_STEP_S = 0.3;
    constexpr double HOLOSMOKE_MID_OPEN_S = 0.5 * SOL::HoloMenu::OPEN_S;       // A capture inside the draw-in
    constexpr double HOLOSMOKE_OPENED_S = SOL::HoloMenu::OPEN_S + 0.2;         // Open, plus a margin
    constexpr double HOLOSMOKE_CLOSED_S = SOL::HoloMenu::CLOSE_S + 0.2;        // Closed, plus a margin
    constexpr double HOLOSMOKE_SETTLED_S = 0.6;                                // Past the hover ramp and flicker
    constexpr double HOLOSMOKE_MID_CLOSE_S = 0.6 * SOL::HoloMenu::CLOSE_S;     // A capture inside the close
    constexpr double HOLOSMOKE_HOVER_GAP_S = 0.25;     // Between the two sector drags (past every hover rate limit)
    constexpr double HOLOSMOKE_WARP_TIMEOUT_S = 20.0;  // Longest wait for the Jump To warp to end
    constexpr double HOLOSMOKE_CURSOR_TIMEOUT_S = 2.0;
    constexpr int32 HOLOSMOKE_CURSOR_PAINTS = 3;                               // Cursor paints after a change
    constexpr double HOLOSMOKE_QUIT_S = 1.0;
    constexpr float HOLOSMOKE_CURSOR_STRENGTH = 0.35f;     // A HoloMenuEffects value that differs from the HUD's 1
    constexpr int32 HOLOSMOKE_PREVIEW_Z_ORDER = SOL::Overlay::MENU_Z_ORDER;
    constexpr int32 HOLOSMOKE_HOVER_ITEM = 2;

    // The frame preview: an Overlay-Menu-like layout in Slate units (the box's size and its offset on the viewport)
    const FVector2f HOLOSMOKE_PREVIEW_OFFSET(60.0f, 60.0f);
    const FVector2f HOLOSMOKE_PREVIEW_SIZE(420.0f, 480.0f);
    constexpr float HOLOSMOKE_TITLE_STEP = 170.0f;
    constexpr float HOLOSMOKE_TAB_LEFT = 14.0f;
    constexpr float HOLOSMOKE_TAB_RIGHT = 62.0f;
    constexpr float HOLOSMOKE_TAB_TOP = 60.0f;
    constexpr float HOLOSMOKE_TAB_PITCH = 36.0f;
    constexpr float HOLOSMOKE_TAB_HEIGHT = 28.0f;
    constexpr float HOLOSMOKE_DIVIDER_X = 74.0f;
    constexpr float HOLOSMOKE_DIVIDER_TOP = 52.0f;
    constexpr float HOLOSMOKE_DIVIDER_BOTTOM = 420.0f;
    constexpr float HOLOSMOKE_HINT_Y = 440.0f;
    constexpr float HOLOSMOKE_HINT_RIGHT = 400.0f;
    const FBox2f HOLOSMOKE_HOVER_ROW(FVector2f(90.0f, 120.0f), FVector2f(390.0f, 140.0f));

    //////////////////////////////////////////////////////////////////////////
    // Returns a cue's name for the log
    const TCHAR* HoloSmokeCueName(const ESOLHoloCue cue)
    {
        switch (cue)
        {
        case ESOLHoloCue::Open:
            return TEXT("open");
        case ESOLHoloCue::Hover:
            return TEXT("hover");
        case ESOLHoloCue::Select:
            return TEXT("select");
        default:
            return TEXT("?");
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two shapes are exactly equal, field by field
    bool HoloSmokeShapeEquals(const FSOLHoloShape& a, const FSOLHoloShape& b)
    {
        return a.Part == b.Part && a.Kind == b.Kind && a.A == b.A && a.B == b.B && a.HalfWidthPx == b.HalfWidthPx
            && a.Color == b.Color && a.Intensity == b.Intensity && a.Fill == b.Fill && a.PadPx == b.PadPx;
    }
}

//////////////////////////////////////////////////////////////////////////
// Removes the frame preview if it is still on the viewport
FSOLHoloMenuSmoke::~FSOLHoloMenuSmoke()
{
    RemoveFramePreview();
}

//////////////////////////////////////////////////////////////////////////
// Caches the controller, HUD and subsystems and starts the first step
void FSOLHoloMenuSmoke::Start(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    mController = pawn.GetController<APlayerController>();
    mHud = mController.IsValid() ? mController->GetHUD<ASOLFlightHud>() : nullptr;
    mHoloMenu = world->GetSubsystem<USOLHoloMenuSubsystem>();
    mSettings = USOLSettingsSubsystem::Get(world);
    mInteraction = world->GetSubsystem<USOLInteractionSubsystem>();
    UE_LOG(LogSOL, Log, TEXT("HoloMenu: scripted hologram menu checks starting (parts 42c-42d)"));
    mStep = 0;
    EnterStep(mStep, pawn);
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script; returns true once it has finished
bool FSOLHoloMenuSmoke::Update(ASOLShipPawn& pawn, const float /*deltaSeconds*/)
{
    if (mStep >= static_cast<int32>(EHoloStep::Count))
    {
        return true;
    }
    if (!mController.IsValid() || !mHoloMenu.IsValid() || !mSettings.IsValid() || !mInteraction.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("HoloMenu: controller, menu, settings or interaction subsystem missing; aborting"));
        mStep = static_cast<int32>(EHoloStep::Count);
        UKismetSystemLibrary::QuitGame(pawn.GetWorld(), nullptr, EQuitPreference::Quit, false);
        return true;
    }
    if (!mHud.IsValid())
    {
        mHud = mController->GetHUD<ASOLFlightHud>();
    }
    ++mStepFrames;
    if (!TickStep(mStep, pawn) || mStepFrames < HOLOSMOKE_MIN_FRAMES)
    {
        return false;
    }
    ExitStep(mStep, pawn);
    ++mStep;
    mStepFrames = 0;
    if (mStep >= static_cast<int32>(EHoloStep::Count))
    {
        return true;
    }
    EnterStep(mStep, pawn);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's action at its start
void FSOLHoloMenuSmoke::EnterStep(const int32 step, ASOLShipPawn& pawn)
{
    mStepStartS = FPlatformTime::Seconds();
    mShotFrame = INDEX_NONE;
    USOLHoloMenuSubsystem* holoMenu = mHoloMenu.Get();
    SSOLHoloFrame* frame = mFrameWidget.IsValid() && mFrameWidget->GetFrame().IsValid()
        ? mFrameWidget->GetFrame().Get() : nullptr;
    switch (static_cast<EHoloStep>(step))
    {
    case EHoloStep::FrameAdd:
        AddFramePreview(pawn);
        break;
    case EHoloStep::FrameOpen:
        mOpenRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Open);
        mMidOpenFraction = -1.0f;
        mIsMidOpenShot = false;
        if (mFrameWidget.IsValid())
        {
            mFrameWidget->BeginOpen();
        }
        break;
    case EHoloStep::FrameHover:
        mHoverRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover);
        if (mFrameWidget.IsValid())
        {
            mFrameWidget->SetHoveredItem(HOLOSMOKE_HOVER_ITEM, HOLOSMOKE_HOVER_ROW, ESOLHoloHoverMark::RowPointer);
        }
        break;
    case EHoloStep::FrameFlat:
        // Strength 0: closed and reopened in one frame, so the next paint must show the whole frame at once
        SetStrength(0.0f);
        mOpenRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Open);
        mFirstFlatFraction = -1.0f;
        mPaintsBefore = frame != nullptr ? frame->GetPaintCount() : 0;
        if (mFrameWidget.IsValid())
        {
            mFrameWidget->BeginClose();
            mFrameWidget->BeginOpen();
        }
        break;
    case EHoloStep::FrameStrong:
        SetStrength(SOL::Settings::HOLO_MENU_EFFECTS_MAX);
        break;
    case EHoloStep::Cursor:
    {
        SetStrength(HOLOSMOKE_CURSOR_STRENGTH);
        const USOLHologramCursorSubsystem* cursor = pawn.GetWorld()->GetSubsystem<USOLHologramCursorSubsystem>();
        mCursorPaintsBefore = cursor != nullptr && cursor->GetCursorWidget().IsValid()
            ? cursor->GetCursorWidget()->GetPaintCount() : 0;
        break;
    }
    case EHoloStep::FrameClose:
        SetStrength(SOL::Settings::HOLO_MENU_EFFECTS_DEFAULT);
        if (mFrameWidget.IsValid())
        {
            mFrameWidget->BeginClose();
        }
        break;
    case EHoloStep::RadialOpen:
        // The script moves the flight cursor from here on (the OS mouse would move the highlight)
        pawn.SetFlightCursorScripted(true);
        mOpenRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Open);
        mMidOpenFraction = -1.0f;
        mIsMidOpenShot = false;
        if (FindRadialBody(pawn))
        {
            // A press without a drag: the pointer rests in the centre, so the menu opens with nothing highlighted
            PressOnRadialBody(pawn);
            pawn.HandleFlightCursorMoved(mInteraction->GetRadial().CenterPx);
            mInteraction->MovePointer(pawn.GetFlightCursorPx());
        }
        break;
    case EHoloStep::RadialHover:
        // Onto Target now (the menu is Open: a hover cue), onto Info later in the step
        mHoverRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover);
        mIsSecondDrag = false;
        mRadialSectorA = DragToRadialAction(pawn, ESOLAction::Target);
        mRadialSectorB = INDEX_NONE;
        break;
    case EHoloStep::RadialSelect:
    case EHoloStep::RadialFlatSelect:
        // Release on Info (the cursor is on it): it runs, and the menu closes
        mSelectRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Select);
        mExecutedBefore = mInteraction->GetExecutedCount();
        mMidCloseFraction = -1.0f;
        mFlatCloseSectors = -1;
        pawn.HandleFire(false);
        break;
    case EHoloStep::RadialFlat:
        SetStrength(0.0f);
        mOpenRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Open);
        mHoverRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover);
        mFirstFlatFraction = -1.0f;
        mFirstFlatFill = -1.0f;
        mFirstFlatSectors = 0;
        PressOnRadialBody(pawn);
        break;
    case EHoloStep::RadialStrong:
        // Opened and dragged onto Info in one frame (no hover cue while it draws in)
        SetStrength(SOL::Settings::HOLO_MENU_EFFECTS_MAX);
        PressOnRadialBody(pawn);
        mRadialSectorB = DragToRadialAction(pawn, ESOLAction::Info);
        break;
    case EHoloStep::RadialCancel:
        mSelectRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Select);
        mExecutedBefore = mInteraction->GetExecutedCount();
        mCancelsBefore = mInteraction->GetRadialCancelCount();
        pawn.HandleFlightCursorMoved(mInteraction->GetRadial().CenterPx);
        mInteraction->MovePointer(pawn.GetFlightCursorPx());
        pawn.HandleFire(false);
        break;
    case EHoloStep::RadialDisabled:
    case EHoloStep::RadialJump:
        // Opened and dragged onto the step's action; released once Open (TickStep)
        mSelectRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Select);
        mExecutedBefore = mInteraction->GetExecutedCount();
        mCancelsBefore = mInteraction->GetRadialCancelCount();
        mReleaseFrame = INDEX_NONE;
        mSelectsAtRelease = -1;
        mIsWarpingAtRelease = false;
        mReleaseRim = -1.0f;
        mReleaseLook = FSOLHoloMenuLook();
        PressOnRadialBody(pawn);
        mRadialSectorB = DragToRadialAction(pawn, static_cast<EHoloStep>(step) == EHoloStep::RadialJump
            ? ESOLAction::JumpTo : ESOLAction::Communicate);
        break;
    case EHoloStep::Stubs:
        LogStubs();
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's per-frame work; returns true once the step is done
bool FSOLHoloMenuSmoke::TickStep(const int32 step, ASOLShipPawn& pawn)
{
    const double elapsedS = FPlatformTime::Seconds() - mStepStartS;
    SSOLHoloFrame* frame = mFrameWidget.IsValid() && mFrameWidget->GetFrame().IsValid()
        ? mFrameWidget->GetFrame().Get() : nullptr;
    const ASOLFlightHud* hud = mHud.Get();
    switch (static_cast<EHoloStep>(step))
    {
    case EHoloStep::Setup:
        return ShotThenDone(TEXT("Start"), elapsedS >= HOLOSMOKE_SETUP_S);
    case EHoloStep::FrameOpen:
        // The draw-in caught about half-way (the last paint's look), with a screenshot of the look
        if (frame != nullptr && mMidOpenFraction < 0.0f && elapsedS >= HOLOSMOKE_MID_OPEN_S)
        {
            mMidOpenFraction = frame->GetLastLook().FrameFraction;
            TakeUiShot(TEXT("FrameMidOpen"));
            mIsMidOpenShot = true;
        }
        return elapsedS >= HOLOSMOKE_OPENED_S;
    case EHoloStep::FrameHover:
        if (mStepFrames == 1 && frame != nullptr)
        {
            mPaintsBefore = frame->GetPaintCount();
            mCacheHitsBefore = frame->GetCacheHitCount();
        }
        return ShotThenDone(TEXT("Frame_S1"), elapsedS >= HOLOSMOKE_SETTLED_S);
    case EHoloStep::FrameFlat:
        // The first paint after the reopen, then the hover set again once the frame reads Open (Advance in paint)
        if (frame != nullptr && mFirstFlatFraction < 0.0f && frame->GetPaintCount() > mPaintsBefore)
        {
            mFirstFlatFraction = frame->GetLastLook().FrameFraction;
            mFrameWidget->SetHoveredItem(HOLOSMOKE_HOVER_ITEM, HOLOSMOKE_HOVER_ROW, ESOLHoloHoverMark::RowPointer);
        }
        return ShotThenDone(TEXT("Frame_S0"), elapsedS >= HOLOSMOKE_SETTLED_S);
    case EHoloStep::FrameStrong:
        return ShotThenDone(TEXT("Frame_S2"), elapsedS >= HOLOSMOKE_SETTLED_S);
    case EHoloStep::Cursor:
    {
        const USOLHologramCursorSubsystem* cursor = pawn.GetWorld()->GetSubsystem<USOLHologramCursorSubsystem>();
        const int32 paints = cursor != nullptr && cursor->GetCursorWidget().IsValid()
            ? cursor->GetCursorWidget()->GetPaintCount() : 0;
        return paints >= mCursorPaintsBefore + HOLOSMOKE_CURSOR_PAINTS || elapsedS >= HOLOSMOKE_CURSOR_TIMEOUT_S;
    }
    case EHoloStep::FrameClose:
        return elapsedS >= HOLOSMOKE_CLOSED_S;
    case EHoloStep::RadialOpen:
        // The rim arcs caught about half-way through the draw-in (the last HUD frame's look), with a screenshot
        if (hud != nullptr && mMidOpenFraction < 0.0f && elapsedS >= HOLOSMOKE_MID_OPEN_S)
        {
            mMidOpenFraction = hud->GetLastRadialLook().FrameFraction;
            TakeUiShot(TEXT("RadialMidOpen"));
            mIsMidOpenShot = true;
        }
        return elapsedS >= HOLOSMOKE_OPENED_S;
    case EHoloStep::RadialHover:
        if (!mIsSecondDrag && elapsedS >= HOLOSMOKE_HOVER_GAP_S)
        {
            mRadialSectorB = DragToRadialAction(pawn, ESOLAction::Info);
            mIsSecondDrag = true;
        }
        return ShotThenDone(TEXT("Radial_S1"), mIsSecondDrag && elapsedS >= HOLOSMOKE_HOVER_GAP_S
            + HOLOSMOKE_SETTLED_S);
    case EHoloStep::RadialSelect:
        // The close caught half-way: drawn from the cached state after the interaction closed the menu
        if (hud != nullptr && mMidCloseFraction < 0.0f && elapsedS >= HOLOSMOKE_MID_CLOSE_S)
        {
            mMidCloseFraction = hud->GetLastRadialLook().FrameFraction;
            mMidClosePhase = hud->GetRadialHolo().Anim.Phase;
            mMidCloseSectors = hud->GetLastRadialSectorCount();
            mMidCloseHighlight = hud->GetRadialHolo().Radial.Highlighted;
            mMidCloseHovered = hud->GetRadialHolo().Anim.HoveredItem;
            mIsMidCloseOpen = mInteraction->IsRadialOpen();
            TakeUiShot(TEXT("RadialClosing"));
        }
        return elapsedS >= HOLOSMOKE_CLOSED_S;
    case EHoloStep::RadialFlat:
        // The first HUD frame after the press, then onto Info
        if (mStepFrames == 1 && hud != nullptr)
        {
            mFirstFlatFraction = hud->GetLastRadialLook().FrameFraction;
            mFirstFlatFill = hud->GetLastRadialLook().FillAlpha;
            mFirstFlatSectors = hud->GetLastRadialSectorCount();
            mRadialSectorB = DragToRadialAction(pawn, ESOLAction::Info);
        }
        return ShotThenDone(TEXT("Radial_S0"), elapsedS >= HOLOSMOKE_SETTLED_S);
    case EHoloStep::RadialFlatSelect:
        // The first HUD frame after the release: closed at once at strength 0
        if (mStepFrames == 1 && hud != nullptr)
        {
            mFlatClosePhase = hud->GetRadialHolo().Anim.Phase;
            mFlatCloseSectors = hud->GetLastRadialSectorCount();
        }
        return elapsedS >= HOLOSMOKE_STEP_S;
    case EHoloStep::RadialStrong:
        return ShotThenDone(TEXT("Radial_S2"), elapsedS >= HOLOSMOKE_SETTLED_S);
    case EHoloStep::RadialCancel:
        return elapsedS >= HOLOSMOKE_CLOSED_S;
    case EHoloStep::RadialDisabled:
    case EHoloStep::RadialJump:
    {
        // Once Open: the highlighted rim, then the release; the next tick reads the cues the release frame asked for
        // (that frame's HUD update ran after the release), then the close (and the warp) plays out
        const USOLJumpSubsystem* jump = pawn.GetWorld()->GetSubsystem<USOLJumpSubsystem>();
        if (mReleaseFrame == INDEX_NONE && elapsedS >= HOLOSMOKE_OPENED_S)
        {
            mReleaseRim = hud != nullptr ? hud->GetLastRadialHighlightIntensity() : -1.0f;
            mReleaseLook = hud != nullptr ? hud->GetLastRadialLook() : FSOLHoloMenuLook();
            pawn.HandleFire(false);
            mReleaseFrame = mStepFrames;
            mReleaseS = elapsedS;
        }
        else if (mReleaseFrame != INDEX_NONE && mStepFrames == mReleaseFrame + 1)
        {
            mSelectsAtRelease = mHoloMenu->GetCueRequestCount(ESOLHoloCue::Select) - mSelectRequestsBefore;
            mIsWarpingAtRelease = jump != nullptr && jump->IsWarping();
        }
        const bool bIsWarping = jump != nullptr && jump->IsWarping();
        return mReleaseFrame != INDEX_NONE && mStepFrames > mReleaseFrame + 1
            && elapsedS >= mReleaseS + HOLOSMOKE_CLOSED_S
            && (!bIsWarping || elapsedS >= mReleaseS + HOLOSMOKE_WARP_TIMEOUT_S);
    }
    case EHoloStep::Quit:
        return elapsedS >= HOLOSMOKE_QUIT_S;
    default:
        return elapsedS >= HOLOSMOKE_STEP_S;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's checks at its end
void FSOLHoloMenuSmoke::ExitStep(const int32 step, ASOLShipPawn& pawn)
{
    using namespace SOL::HoloMenu;
    USOLHoloMenuSubsystem* holoMenu = mHoloMenu.Get();
    const ASOLFlightHud* hud = mHud.Get();
    UMaterialInstanceDynamic* menuMaterial = holoMenu->GetMenuMaterial();
    SSOLHoloFrame* frame = mFrameWidget.IsValid() && mFrameWidget->GetFrame().IsValid()
        ? mFrameWidget->GetFrame().Get() : nullptr;
    switch (static_cast<EHoloStep>(step))
    {
    case EHoloStep::Setup:
    {
        // The subsystem's material, made from the HUD shape material, with the live strength and the view size
        const FString parent = menuMaterial != nullptr && menuMaterial->Parent != nullptr
            ? menuMaterial->Parent->GetPathName() : FString(TEXT("none"));
        Check(TEXT("a-subsystem"), menuMaterial != nullptr && parent == SOL::Paths::HUD_SHAPE_MATERIAL,
            FString::Printf(TEXT("menu material %s, parent %s"), menuMaterial != nullptr ? *menuMaterial->GetName()
                : TEXT("none"), *parent));
        const float live = mSettings->GetLiveValues().HoloMenuEffects;
        const float strengthParam = GetMenuParam(SOL::HudShapeMaterialParams::EFFECTS_STRENGTH);
        const FVector2f viewSize = holoMenu->GetViewSizePx();
        const float widthParam = GetMenuParam(SOL::HudShapeMaterialParams::VIEW_WIDTH);
        const float heightParam = GetMenuParam(SOL::HudShapeMaterialParams::VIEW_HEIGHT);
        Check(TEXT("a-mid-params"), strengthParam == live && holoMenu->GetStrength() == live
            && widthParam == viewSize.X && heightParam == viewSize.Y && viewSize.X > 0.0f,
            FString::Printf(TEXT("HoloMenuEffects %.2f, material strength %.2f, view %.0fx%.0f, material view "
                "%.0fx%.0f, %d pushes"), live, strengthParam, viewSize.X, viewSize.Y, widthParam, heightParam,
                holoMenu->GetParamPushCount()));

        // The menu layer: there, with the menu material, and nothing on it or stamped with no menu open
        const uint64 stamp = hud != nullptr ? hud->GetMenuLayerBatchFrame() : 0;
        const bool bIsEmpty = hud != nullptr && hud->GetLastMenuShapeCount() == 0
            && hud->GetLastMenuBackingCount() == 0 && hud->GetLastMenuPaintedShapeCount() == 0;
        Check(TEXT("a-menu-layer"), hud != nullptr && hud->HasMenuLayer() && hud->GetMenuLayerMaterial() == menuMaterial
            && bIsEmpty && stamp == 0, FString::Printf(TEXT("menu layer %s, material %s, %d shapes, %d backings, "
                "%d painted, stamped at frame %llu (now %llu)"), hud != nullptr && hud->HasMenuLayer() ? TEXT("present")
                : TEXT("MISSING"), hud != nullptr && hud->GetMenuLayerMaterial() == menuMaterial ? TEXT("menu MID")
                : TEXT("OTHER"), hud != nullptr ? hud->GetLastMenuShapeCount() : -1,
                hud != nullptr ? hud->GetLastMenuBackingCount() : -1,
                hud != nullptr ? hud->GetLastMenuPaintedShapeCount() : -1, stamp, GFrameCounter));
        Check(TEXT("a-hud-layer"), hud != nullptr && hud->WasShapeRendererActive() && hud->GetLastShapeCount() > 0
            && hud->GetLastPanelCount() > 0, FString::Printf(TEXT("HUD layer: renderer %s, %d shapes, %d panels, "
                "%d texts"), hud != nullptr && hud->WasShapeRendererActive() ? TEXT("on") : TEXT("off"),
                hud != nullptr ? hud->GetLastShapeCount() : 0, hud != nullptr ? hud->GetLastPanelCount() : 0,
                hud != nullptr ? hud->GetLastTextCount() : 0));
        break;
    }
    case EHoloStep::Cues:
    {
        // One request of each cue: requested, accepted (first or past its interval) and started (asset loaded)
        const double nowS = FPlatformTime::Seconds();
        int32 moved = 0;
        FString detail;
        for (int32 index = 0; index < static_cast<int32>(ESOLHoloCue::Count); ++index)
        {
            const ESOLHoloCue cue = static_cast<ESOLHoloCue>(index);
            const int32 requested = holoMenu->GetCueRequestCount(cue);
            const int32 accepted = holoMenu->GetCueAcceptedCount(cue);
            const int32 played = holoMenu->GetCuePlayedCount(cue);
            holoMenu->PlayCue(cue, nowS);
            const bool bIsMoved = holoMenu->GetCueRequestCount(cue) == requested + 1
                && holoMenu->GetCueAcceptedCount(cue) == accepted + 1 && holoMenu->GetCuePlayedCount(cue) == played + 1;
            moved += bIsMoved ? 1 : 0;
            detail += FString::Printf(TEXT("%s %d/%d/%d; "), HoloSmokeCueName(cue), holoMenu->GetCueRequestCount(cue),
                holoMenu->GetCueAcceptedCount(cue), holoMenu->GetCuePlayedCount(cue));
        }
        Check(TEXT("l-cues"), moved == static_cast<int32>(ESOLHoloCue::Count), FString::Printf(TEXT("%d of %d cues "
            "requested, accepted and started (requested/accepted/started: %s)"), moved,
            static_cast<int32>(ESOLHoloCue::Count), *detail));

        // A repeat inside the rate limit is counted but not accepted
        const int32 accepted = holoMenu->GetCueAcceptedCount(ESOLHoloCue::Hover);
        const bool bIsAccepted = holoMenu->PlayCue(ESOLHoloCue::Hover, nowS);
        Check(TEXT("l-cue-rate"), !bIsAccepted && holoMenu->GetCueAcceptedCount(ESOLHoloCue::Hover) == accepted,
            FString::Printf(TEXT("hover again at once: %s (accepted %d)"), bIsAccepted ? TEXT("ACCEPTED")
                : TEXT("rejected"), holoMenu->GetCueAcceptedCount(ESOLHoloCue::Hover)));
        break;
    }
    case EHoloStep::FrameAdd:
        Check(TEXT("k-frame-built"), frame != nullptr && frame->GetPaintCount() > 0
            && frame->GetAnim().Phase == ESOLHoloMenuPhase::Closed && frame->GetLastPaintedShapeCount() == 0,
            FString::Printf(TEXT("preview %s, %d paints, phase %d, %d shapes drawn"), frame != nullptr
                ? TEXT("built") : TEXT("MISSING"), frame != nullptr ? frame->GetPaintCount() : 0,
                frame != nullptr ? static_cast<int32>(frame->GetAnim().Phase) : -1,
                frame != nullptr ? frame->GetLastPaintedShapeCount() : -1));
        break;
    case EHoloStep::FrameOpen:
    {
        const int32 opens = holoMenu->GetCueRequestCount(ESOLHoloCue::Open) - mOpenRequestsBefore;
        Check(TEXT("k-frame-open-cue"), opens == 1, FString::Printf(TEXT("%d open cue requests at BeginOpen"),
            opens));
        Check(TEXT("k-frame-mid-open"), mIsMidOpenShot && mMidOpenFraction > 0.0f && mMidOpenFraction < 1.0f,
            FString::Printf(TEXT("frame fraction %.3f at %.2f s"), mMidOpenFraction, HOLOSMOKE_MID_OPEN_S));
        const int32 outline = CountPart(ESOLHoloPart::Outline);
        const bool bIsOpen = frame != nullptr && frame->GetAnim().Phase == ESOLHoloMenuPhase::Open;
        Check(TEXT("k-frame-drawn"), bIsOpen && outline > 0 && CountPart(ESOLHoloPart::GapDot) > 0
            && frame->GetLastPaintedShapeCount() == frame->GetLastOutput().ShapeCount
            && frame->GetLastPaintedBackingCount() == 1 && frame->GetMaterial() == menuMaterial
            && frame->GetLastLook().FrameFraction == 1.0f && IsFrameOutputFresh(),
            FString::Printf(TEXT("phase %s, %d shapes drawn (%d outline, %d gap dots, %d ruler, %d tabs), %d backing, "
                "material %s"), bIsOpen ? TEXT("Open") : TEXT("NOT open"), frame != nullptr
                ? frame->GetLastPaintedShapeCount() : 0, outline, CountPart(ESOLHoloPart::GapDot),
                CountPart(ESOLHoloPart::Ruler), CountPart(ESOLHoloPart::Tab), frame != nullptr
                ? frame->GetLastPaintedBackingCount() : 0, frame != nullptr && frame->GetMaterial() == menuMaterial
                ? TEXT("menu MID") : TEXT("OTHER")));
        break;
    }
    case EHoloStep::FrameHover:
    {
        const int32 hovers = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover) - mHoverRequestsBefore;
        const int32 hoverShapes = CountPart(ESOLHoloPart::Hover);
        Check(TEXT("k-frame-hover"), hovers == 1 && hoverShapes > 0 && CountPart(ESOLHoloPart::HoverDot) == 1,
            FString::Printf(TEXT("%d hover cue requests, %d hover pieces and %d dot drawn"), hovers, hoverShapes,
                CountPart(ESOLHoloPart::HoverDot)));
        const int32 paints = frame != nullptr ? frame->GetPaintCount() - mPaintsBefore : 0;
        const int32 hits = frame != nullptr ? frame->GetCacheHitCount() - mCacheHitsBefore : 0;
        Check(TEXT("k-frame-reuse"), paints > 0 && hits > paints / 2 && IsFrameOutputFresh(),
            FString::Printf(TEXT("%d of %d paints reused the build (open, idle); the drawn frame %s a fresh build"),
                hits, paints, IsFrameOutputFresh() ? TEXT("equals") : TEXT("DIFFERS FROM")));
        break;
    }
    case EHoloStep::FrameFlat:
    {
        const int32 opens = holoMenu->GetCueRequestCount(ESOLHoloCue::Open) - mOpenRequestsBefore;
        const FSOLHoloFrameOutput* output = frame != nullptr ? &frame->GetLastOutput() : nullptr;
        const bool bIsFlatBacking = output != nullptr && output->bHasBacking
            && output->Backing.Alpha == FLAT_PANEL_ALPHA;
        Check(TEXT("k-strength0-open"), mFirstFlatFraction == 1.0f && opens == 1, FString::Printf(TEXT("frame "
            "fraction %.3f on the first paint after the reopen, %d open cue requests (D21)"), mFirstFlatFraction,
            opens));
        Check(TEXT("k-strength0-flat"), frame != nullptr && frame->GetLastStrength() == 0.0f
            && CountPart(ESOLHoloPart::Sweep) == 0 && bIsFlatBacking && AllLinePads(FRAME_FLAT_PAD_PX)
            && CountPart(ESOLHoloPart::Outline) > 0 && CountPart(ESOLHoloPart::Hover) > 0,
            FString::Printf(TEXT("strength %.2f, %d sweeps, backing alpha %.2f, flat pads %s, %d outline, %d hover"),
                frame != nullptr ? frame->GetLastStrength() : -1.0f, CountPart(ESOLHoloPart::Sweep),
                output != nullptr && output->bHasBacking ? output->Backing.Alpha : -1.0f,
                AllLinePads(FRAME_FLAT_PAD_PX) ? TEXT("yes") : TEXT("NO"), CountPart(ESOLHoloPart::Outline),
                CountPart(ESOLHoloPart::Hover)));
        break;
    }
    case EHoloStep::FrameStrong:
    {
        const FSOLHoloFrameOutput* output = frame != nullptr ? &frame->GetLastOutput() : nullptr;
        const float alpha = output != nullptr && output->bHasBacking ? output->Backing.Alpha : -1.0f;
        Check(TEXT("k-strength2"), frame != nullptr && frame->GetLastStrength() == SOL::Settings::HOLO_MENU_EFFECTS_MAX
            && CountPart(ESOLHoloPart::Sweep) == 1 && AllLinePads(SOL::Hologram::HUD_GLOW_PAD_PX)
            && FMath::IsNearlyEqual(alpha, 2.0f * BACKDROP_ALPHA_PER_UNIT)
            && GetMenuParam(SOL::HudShapeMaterialParams::EFFECTS_STRENGTH) == SOL::Settings::HOLO_MENU_EFFECTS_MAX,
            FString::Printf(TEXT("strength %.2f, %d sweep, glow pads %s, backing alpha %.2f, material strength %.2f"),
                frame != nullptr ? frame->GetLastStrength() : -1.0f, CountPart(ESOLHoloPart::Sweep),
                AllLinePads(SOL::Hologram::HUD_GLOW_PAD_PX) ? TEXT("yes") : TEXT("NO"), alpha,
                GetMenuParam(SOL::HudShapeMaterialParams::EFFECTS_STRENGTH)));
        break;
    }
    case EHoloStep::Cursor:
    {
        // D19 (section 12 step 10, the cursor part): the cursor's material strength is HoloMenuEffects
        const USOLHologramCursorSubsystem* cursor = pawn.GetWorld()->GetSubsystem<USOLHologramCursorSubsystem>();
        const TSharedPtr<SSOLHologramCursor> widget = cursor != nullptr ? cursor->GetCursorWidget() : nullptr;
        const float pushed = widget.IsValid() ? widget->GetPushedStrength() : -1.0f;
        const float hudStrength = mSettings->GetLiveValues().HudEffectsStrength;
        const int32 paints = widget.IsValid() ? widget->GetPaintCount() - mCursorPaintsBefore : 0;
        Check(TEXT("j-cursor-strength"), paints > 0 && pushed == HOLOSMOKE_CURSOR_STRENGTH
            && hudStrength != HOLOSMOKE_CURSOR_STRENGTH
            && GetMenuParam(SOL::HudShapeMaterialParams::EFFECTS_STRENGTH) == HOLOSMOKE_CURSOR_STRENGTH,
            FString::Printf(TEXT("HoloMenuEffects %.2f: cursor strength %.2f after %d paints, HUD effects %.2f, menu "
                "material %.2f"), HOLOSMOKE_CURSOR_STRENGTH, pushed, paints, hudStrength,
                GetMenuParam(SOL::HudShapeMaterialParams::EFFECTS_STRENGTH)));
        break;
    }
    case EHoloStep::FrameClose:
    {
        const bool bIsClosed = frame != nullptr && frame->GetAnim().Phase == ESOLHoloMenuPhase::Closed;
        Check(TEXT("k-frame-close"), bIsClosed && frame->GetLastPaintedShapeCount() == 0
            && frame->GetLastPaintedBackingCount() == 0, FString::Printf(TEXT("after %.2f s: phase %s, %d shapes, "
                "%d backings drawn"), HOLOSMOKE_CLOSED_S, bIsClosed ? TEXT("Closed") : TEXT("NOT closed"),
                frame != nullptr ? frame->GetLastPaintedShapeCount() : -1,
                frame != nullptr ? frame->GetLastPaintedBackingCount() : -1));
        RemoveFramePreview();
        Check(TEXT("k-menu-layer-idle"), hud != nullptr && hud->GetLastMenuShapeCount() == 0
            && hud->GetMenuLayerBatchFrame() == 0, FString::Printf(TEXT("menu layer after the frame preview: %d "
                "shapes, stamped at frame %llu (the UMG frame never draws on it)"), hud != nullptr
                ? hud->GetLastMenuShapeCount() : -1, hud != nullptr ? hud->GetMenuLayerBatchFrame() : 0));
        break;
    }
    case EHoloStep::RadialOpen:
    {
        // Step 5: the open cue at the press, the draw-in caught mid-way, then Open with every sector on the menu layer
        const FSOLRadialMenuState& radial = mInteraction->GetRadial();
        const int32 opens = holoMenu->GetCueRequestCount(ESOLHoloCue::Open) - mOpenRequestsBefore;
        const FSOLHoloMenuLook look = hud != nullptr ? hud->GetLastRadialLook() : FSOLHoloMenuLook();
        const bool bIsOpen = hud != nullptr && hud->GetRadialHolo().Anim.Phase == ESOLHoloMenuPhase::Open;
        const int32 sectors = hud != nullptr ? hud->GetLastRadialSectorCount() : 0;
        const int32 menuShapes = hud != nullptr ? hud->GetLastMenuShapeCount() : 0;
        Check(TEXT("r-radial-open"), radial.bOpen && opens == 1 && bIsOpen && sectors > 0 && sectors == radial.Count
            && look.FrameFraction == 1.0f && look.FillAlpha == 1.0f && menuShapes >= 2 * sectors,
            FString::Printf(TEXT("radial %s on body %d, %d open cue requests, phase %s, %d of %d sectors drawn, frame "
                "%.2f, fill %.2f, %d menu layer shapes"), radial.bOpen ? TEXT("open") : TEXT("CLOSED"),
                mRadialBody.Index, opens, bIsOpen ? TEXT("Open") : TEXT("NOT open"), sectors, radial.Count,
                look.FrameFraction, look.FillAlpha, menuShapes));
        Check(TEXT("r-radial-mid-open"), mIsMidOpenShot && mMidOpenFraction > 0.0f && mMidOpenFraction < 1.0f,
            FString::Printf(TEXT("rim arc draw-in %.3f at %.2f s"), mMidOpenFraction, HOLOSMOKE_MID_OPEN_S));
        break;
    }
    case EHoloStep::RadialHover:
    {
        // Two sector changes while Open: two hover cues, the glow on top of the highlight, full glow pads at 1
        const FSOLRadialMenuState& radial = mInteraction->GetRadial();
        const int32 hovers = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover) - mHoverRequestsBefore;
        const FSOLHoloMenuLook look = hud != nullptr ? hud->GetLastRadialLook() : FSOLHoloMenuLook();
        const float rim = hud != nullptr ? hud->GetLastRadialHighlightIntensity() : -1.0f;
        const int32 hovered = hud != nullptr ? hud->GetRadialHolo().Anim.HoveredItem : INDEX_NONE;
        const float padPx = hud != nullptr ? hud->GetLastRadialPadPx() : -1.0f;
        Check(TEXT("r-radial-hover"), hovers == 2 && mRadialSectorA != INDEX_NONE && mRadialSectorB != INDEX_NONE
            && mRadialSectorA != mRadialSectorB && radial.Highlighted == mRadialSectorB && hovered == mRadialSectorB
            && look.HoverIntensity > 0.0f && rim > SOL::Interact::RADIAL_HIGHLIGHT_INTENSITY
            && FMath::IsNearlyEqual(rim, SOL::Interact::RADIAL_HIGHLIGHT_INTENSITY
                + look.HoverIntensity * look.FillAlpha)
            && FMath::IsNearlyEqual(padPx, SOL::Hologram::HUD_GLOW_PAD_PX),
            FString::Printf(TEXT("%d hover cue requests over sectors %d then %d; highlighted %d, hovered %d, hover "
                "glow %.2f, highlighted rim %.2f, arc pad %.2f"), hovers, mRadialSectorA, mRadialSectorB,
                radial.Highlighted, hovered, look.HoverIntensity, rim, padPx));
        break;
    }
    case EHoloStep::RadialSelect:
    {
        // The release ran Info with the select cue; the close was drawn from the cached state, then nothing
        const int32 selects = holoMenu->GetCueRequestCount(ESOLHoloCue::Select) - mSelectRequestsBefore;
        const int32 executed = mInteraction->GetExecutedCount() - mExecutedBefore;
        const ESOLAction last = mInteraction->GetLastExecutedAction();
        Check(TEXT("r-radial-select"), selects == 1 && executed == 1 && last == ESOLAction::Info
            && !mInteraction->IsRadialOpen(), FString::Printf(TEXT("release on Info: %d select cue requests, %d "
                "action(s), last %s, menu %s"), selects, executed, SOLActionTable::ActionLabel(last),
                mInteraction->IsRadialOpen() ? TEXT("OPEN") : TEXT("closed")));
        const bool bIsClosed = hud != nullptr && hud->GetRadialHolo().Anim.Phase == ESOLHoloMenuPhase::Closed;
        const int32 sectors = hud != nullptr ? hud->GetLastRadialSectorCount() : -1;
        Check(TEXT("r-radial-close"), !mIsMidCloseOpen && mMidClosePhase == ESOLHoloMenuPhase::Closing
            && mMidCloseFraction > 0.0f && mMidCloseFraction < 1.0f && mMidCloseSectors > 0
            && mMidCloseHighlight == mRadialSectorB && mMidCloseHovered == mRadialSectorB && bIsClosed && sectors == 0,
            FString::Printf(TEXT("mid-close (menu %s): phase %d, draw-in %.3f, %d sectors from the cache, cached "
                "highlight %d, hovered %d (Info %d, the glow fades with the fill); after %.2f s: phase %s, %d "
                "sectors"), mIsMidCloseOpen ? TEXT("OPEN") : TEXT("closed"), static_cast<int32>(mMidClosePhase),
                mMidCloseFraction, mMidCloseSectors, mMidCloseHighlight, mMidCloseHovered, mRadialSectorB,
                HOLOSMOKE_CLOSED_S, bIsClosed ? TEXT("Closed") : TEXT("NOT closed"), sectors));
        break;
    }
    case EHoloStep::RadialFlat:
    {
        // Step 9, the radial at strength 0 (D18): whole on the first frame, flat arcs, no glow; the cues still asked
        const FSOLRadialMenuState& radial = mInteraction->GetRadial();
        const int32 opens = holoMenu->GetCueRequestCount(ESOLHoloCue::Open) - mOpenRequestsBefore;
        const int32 hovers = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover) - mHoverRequestsBefore;
        const FSOLHoloMenuLook look = hud != nullptr ? hud->GetLastRadialLook() : FSOLHoloMenuLook();
        const float rim = hud != nullptr ? hud->GetLastRadialHighlightIntensity() : -1.0f;
        const float padPx = hud != nullptr ? hud->GetLastRadialPadPx() : -1.0f;
        const float strength = hud != nullptr ? hud->GetLastRadialStrength() : -1.0f;
        Check(TEXT("r-strength0-open"), mFirstFlatFraction == 1.0f && mFirstFlatFill == 1.0f && mFirstFlatSectors > 0
            && opens == 1, FString::Printf(TEXT("first frame after the press: draw-in %.2f, fill %.2f, %d sectors; %d "
                "open cue requests (D21)"), mFirstFlatFraction, mFirstFlatFill, mFirstFlatSectors, opens));
        Check(TEXT("r-strength0-flat"), strength == 0.0f && radial.bOpen && radial.Highlighted == mRadialSectorB
            && mRadialSectorB != INDEX_NONE && look.HoverIntensity == 0.0f
            && rim == SOL::Interact::RADIAL_HIGHLIGHT_INTENSITY && padPx == SOL::HoloMenu::FRAME_FLAT_PAD_PX
            && hovers == 1 && GetMenuParam(SOL::HudShapeMaterialParams::EFFECTS_STRENGTH) == 0.0f,
            FString::Printf(TEXT("strength %.2f, highlighted %d (Info %d), hover glow %.2f, highlighted rim %.2f, arc "
                "pad %.2f, %d hover cue requests (D21), material strength %.2f"), strength, radial.Highlighted,
                mRadialSectorB, look.HoverIntensity, rim, padPx, hovers,
                GetMenuParam(SOL::HudShapeMaterialParams::EFFECTS_STRENGTH)));
        break;
    }
    case EHoloStep::RadialFlatSelect:
    {
        // A strength-0 selection still works (D18) with its select cue (D21), and the menu closes at once
        const int32 selects = holoMenu->GetCueRequestCount(ESOLHoloCue::Select) - mSelectRequestsBefore;
        const int32 executed = mInteraction->GetExecutedCount() - mExecutedBefore;
        const ESOLAction last = mInteraction->GetLastExecutedAction();
        Check(TEXT("r-strength0-select"), selects == 1 && executed == 1 && last == ESOLAction::Info
            && mFlatClosePhase == ESOLHoloMenuPhase::Closed && mFlatCloseSectors == 0,
            FString::Printf(TEXT("release on Info: %d select cue requests, %d action(s), last %s; the next frame: "
                "phase %d, %d sectors"), selects, executed, SOLActionTable::ActionLabel(last),
                static_cast<int32>(mFlatClosePhase), mFlatCloseSectors));
        break;
    }
    case EHoloStep::RadialStrong:
    {
        const FSOLRadialMenuState& radial = mInteraction->GetRadial();
        const FSOLHoloMenuLook look = hud != nullptr ? hud->GetLastRadialLook() : FSOLHoloMenuLook();
        const float rim = hud != nullptr ? hud->GetLastRadialHighlightIntensity() : -1.0f;
        const float padPx = hud != nullptr ? hud->GetLastRadialPadPx() : -1.0f;
        const float strength = hud != nullptr ? hud->GetLastRadialStrength() : -1.0f;
        Check(TEXT("r-strength2"), strength == SOL::Settings::HOLO_MENU_EFFECTS_MAX && radial.bOpen
            && radial.Highlighted == mRadialSectorB && mRadialSectorB != INDEX_NONE
            && FMath::IsNearlyEqual(look.HoverIntensity, HOVER_GLOW_INTENSITY)
            && rim > SOL::Interact::RADIAL_HIGHLIGHT_INTENSITY
            && FMath::IsNearlyEqual(padPx, SOL::Hologram::HUD_GLOW_PAD_PX)
            && GetMenuParam(SOL::HudShapeMaterialParams::EFFECTS_STRENGTH) == SOL::Settings::HOLO_MENU_EFFECTS_MAX,
            FString::Printf(TEXT("strength %.2f, highlighted %d (Info %d), hover glow %.2f, highlighted rim %.2f, arc "
                "pad %.2f, material strength %.2f"), strength, radial.Highlighted, mRadialSectorB, look.HoverIntensity,
                rim, padPx, GetMenuParam(SOL::HudShapeMaterialParams::EFFECTS_STRENGTH)));
        break;
    }
    case EHoloStep::RadialCancel:
    {
        // A release in the centre cancels: no select cue, nothing runs, and the close plays out
        const int32 selects = holoMenu->GetCueRequestCount(ESOLHoloCue::Select) - mSelectRequestsBefore;
        const int32 executed = mInteraction->GetExecutedCount() - mExecutedBefore;
        const int32 cancels = mInteraction->GetRadialCancelCount() - mCancelsBefore;
        const bool bIsClosed = hud != nullptr && hud->GetRadialHolo().Anim.Phase == ESOLHoloMenuPhase::Closed;
        Check(TEXT("r-radial-cancel"), selects == 0 && executed == 0 && cancels == 1 && !mInteraction->IsRadialOpen()
            && bIsClosed && hud->GetLastRadialSectorCount() == 0, FString::Printf(TEXT("release in the centre: %d "
                "cancel(s), %d action(s), %d select cue requests, phase %s"), cancels, executed, selects, bIsClosed
                ? TEXT("Closed") : TEXT("NOT closed")));
        SetStrength(SOL::Settings::HOLO_MENU_EFFECTS_DEFAULT);
        break;
    }
    case EHoloStep::RadialDisabled:
    {
        // A release on a disabled sector runs nothing and asks no select cue (SDD 5); its highlighted rim glows by
        // RADIAL_DISABLED_HOVER_GLOW_SCALE (pending the user's answer, SDD 13 Q13)
        const int32 selects = holoMenu->GetCueRequestCount(ESOLHoloCue::Select) - mSelectRequestsBefore;
        const int32 executed = mInteraction->GetExecutedCount() - mExecutedBefore;
        const int32 cancels = mInteraction->GetRadialCancelCount() - mCancelsBefore;
        const bool bIsClosed = hud != nullptr && hud->GetRadialHolo().Anim.Phase == ESOLHoloMenuPhase::Closed;
        const float expectedRim = SOL::Interact::RADIAL_DISABLED_INTENSITY + mReleaseLook.HoverIntensity
            * FMath::Clamp(mReleaseLook.FillAlpha, 0.0f, 1.0f) * RADIAL_DISABLED_HOVER_GLOW_SCALE;
        Check(TEXT("r-radial-disabled"), mRadialSectorB != INDEX_NONE && mSelectsAtRelease == 0 && selects == 0
            && executed == 0 && cancels == 1 && !mInteraction->IsRadialOpen() && bIsClosed
            && FMath::IsNearlyEqual(mReleaseRim, expectedRim, 1.0e-3f),
            FString::Printf(TEXT("release on Communications (sector %d, disabled): %d select cue requests (%d on the "
                "release frame), %d action(s), %d cancel(s), phase %s; highlighted rim %.3f (expected %.3f)"),
                mRadialSectorB, selects, mSelectsAtRelease, executed, cancels, bIsClosed ? TEXT("Closed")
                : TEXT("NOT closed"), mReleaseRim, expectedRim));
        break;
    }
    case EHoloStep::RadialJump:
    {
        // Jump To hides the flight HUD at its release (the warp starts): the select cue is still asked on that frame,
        // and not again on arrival
        const int32 selects = holoMenu->GetCueRequestCount(ESOLHoloCue::Select) - mSelectRequestsBefore;
        const int32 executed = mInteraction->GetExecutedCount() - mExecutedBefore;
        const ESOLAction last = mInteraction->GetLastExecutedAction();
        const USOLJumpSubsystem* jump = pawn.GetWorld()->GetSubsystem<USOLJumpSubsystem>();
        const bool bIsWarping = jump != nullptr && jump->IsWarping();
        Check(TEXT("r-radial-jump-select"), mRadialSectorB != INDEX_NONE && mSelectsAtRelease == 1
            && mIsWarpingAtRelease && selects == 1 && executed == 1 && last == ESOLAction::JumpTo && !bIsWarping,
            FString::Printf(TEXT("release on Jump To (sector %d): %d select cue requests on the release frame (warp "
                "%s then), %d after the warp (%s), %d action(s), last %s"), mRadialSectorB, mSelectsAtRelease,
                mIsWarpingAtRelease ? TEXT("running") : TEXT("NOT running"), selects, bIsWarping ? TEXT("STILL warping")
                : TEXT("ended"), executed, SOLActionTable::ActionLabel(last)));
        pawn.SetFlightCursorScripted(false);
        break;
    }
    case EHoloStep::Quit:
        UE_LOG(LogSOL, Log, TEXT("HoloMenu SUMMARY: %d of %d checks passed (parts 42c-42d; the other menu steps are "
            "stubs)"), mChecksPassed, mChecksRun);
        UKismetSystemLibrary::QuitGame(pawn.GetWorld(), nullptr, EQuitPreference::Quit, false);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Once ready, requests the step's screenshot, then reports done a few frames later (so the next step's changes never
// land in the shot)
bool FSOLHoloMenuSmoke::ShotThenDone(const TCHAR* name, const bool bIsReady)
{
    if (!bIsReady)
    {
        return false;
    }
    if (mShotFrame == INDEX_NONE)
    {
        TakeUiShot(name);
        mShotFrame = mStepFrames;
        return false;
    }
    return mStepFrames >= mShotFrame + HOLOSMOKE_SHOT_FRAMES;
}

//////////////////////////////////////////////////////////////////////////
// Sets the live (pending, never saved) HoloMenuEffects, as the Settings slider previews it
void FSOLHoloMenuSmoke::SetStrength(const float strength) const
{
    SOLSettingsModel::FSettingsValues values = mSettings->GetLiveValues();
    values.HoloMenuEffects = strength;
    mSettings->SetPendingValues(values);
    UE_LOG(LogSOL, Log, TEXT("HoloMenu: HoloMenuEffects set to %.2f (live, not saved)"), strength);
}

//////////////////////////////////////////////////////////////////////////
// Creates the frame preview, sets its Overlay-Menu-like layout and puts it on the viewport
void FSOLHoloMenuSmoke::AddFramePreview(ASOLShipPawn& pawn)
{
    UGameViewportClient* viewport = pawn.GetWorld()->GetGameViewport();
    if (!mController.IsValid() || viewport == nullptr)
    {
        return;
    }
    mFrameWidget.Reset(NewObject<USOLHoloFrameWidget>(mController.Get()));

    // Five profile tabs on the left with the first selected, the dashed column divider open beside it, a hint divider
    FSOLHoloFrameSpec layout;
    layout.TitleStepWidthPx = HOLOSMOKE_TITLE_STEP;
    layout.TabCount = SOL::HoloMenu::FRAME_MAX_TABS;
    for (int32 tab = 0; tab < layout.TabCount; ++tab)
    {
        const float top = HOLOSMOKE_TAB_TOP + HOLOSMOKE_TAB_PITCH * static_cast<float>(tab);
        layout.TabRects[tab] = FBox2f(FVector2f(HOLOSMOKE_TAB_LEFT, top),
            FVector2f(HOLOSMOKE_TAB_RIGHT, top + HOLOSMOKE_TAB_HEIGHT));
    }
    layout.SelectedTab = 0;
    layout.TabDividerX = HOLOSMOKE_DIVIDER_X;
    layout.bColumnDivider = true;
    layout.DividerTop = FVector2f(HOLOSMOKE_DIVIDER_X, HOLOSMOKE_DIVIDER_TOP);
    layout.DividerBottomY = HOLOSMOKE_DIVIDER_BOTTOM;
    layout.OpenTopY = layout.TabRects[0].Min.Y;
    layout.OpenBottomY = layout.TabRects[0].Max.Y;
    layout.bBrokenDivider = true;
    layout.BrokenStart = FVector2f(HOLOSMOKE_TAB_LEFT, HOLOSMOKE_HINT_Y);
    layout.BrokenEndX = HOLOSMOKE_HINT_RIGHT;
    mFrameWidget->SetLayout(layout);

    // A fixed-size box at a fixed offset, above the HUD like the menus
    mPreviewRoot = SNew(SOverlay)
        + SOverlay::Slot()
        .HAlign(HAlign_Left)
        .VAlign(VAlign_Top)
        .Padding(FMargin(HOLOSMOKE_PREVIEW_OFFSET.X, HOLOSMOKE_PREVIEW_OFFSET.Y, 0.0f, 0.0f))
        [
            SNew(SBox)
            .WidthOverride(HOLOSMOKE_PREVIEW_SIZE.X)
            .HeightOverride(HOLOSMOKE_PREVIEW_SIZE.Y)
            [
                mFrameWidget->TakeWidget()
            ]
        ];
    viewport->AddViewportWidgetContent(mPreviewRoot.ToSharedRef(), HOLOSMOKE_PREVIEW_Z_ORDER);
    mPreviewViewport = viewport;
    UE_LOG(LogSOL, Log, TEXT("HoloMenu: frame preview added (%.0fx%.0f Slate units)"), HOLOSMOKE_PREVIEW_SIZE.X,
        HOLOSMOKE_PREVIEW_SIZE.Y);
}

//////////////////////////////////////////////////////////////////////////
// Takes the frame preview off the viewport it was added to (cached, so it works after the controller went away)
void FSOLHoloMenuSmoke::RemoveFramePreview()
{
    if (mPreviewRoot.IsValid())
    {
        if (UGameViewportClient* viewport = mPreviewViewport.Get())
        {
            viewport->RemoveViewportWidgetContent(mPreviewRoot.ToSharedRef());
        }
        mPreviewRoot.Reset();
    }
    mPreviewViewport.Reset();
    mFrameWidget.Reset();
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the preview's last drawn frame equals a fresh BuildFrame of the same inputs
bool FSOLHoloMenuSmoke::IsFrameOutputFresh()
{
    const SSOLHoloFrame* frame = mFrameWidget.IsValid() && mFrameWidget->GetFrame().IsValid()
        ? mFrameWidget->GetFrame().Get() : nullptr;
    if (frame == nullptr)
    {
        return false;
    }
    const FSOLHoloFrameOutput& drawn = frame->GetLastOutput();
    SOLHoloFrameBuild::BuildFrame(frame->GetLastPixelSpec(), frame->GetLastLook(), frame->GetLastStrength(),
        mFreshOutput);
    if (drawn.ShapeCount != mFreshOutput.ShapeCount || drawn.bHasBacking != mFreshOutput.bHasBacking)
    {
        return false;
    }
    if (drawn.bHasBacking && (drawn.Backing.Rect.Min != mFreshOutput.Backing.Rect.Min
        || drawn.Backing.Rect.Max != mFreshOutput.Backing.Rect.Max || drawn.Backing.Alpha != mFreshOutput.Backing.Alpha
        || drawn.Backing.FeatherPx != mFreshOutput.Backing.FeatherPx))
    {
        return false;
    }
    for (int32 index = 0; index < drawn.ShapeCount; ++index)
    {
        if (!HoloSmokeShapeEquals(drawn.Shapes[index], mFreshOutput.Shapes[index]))
        {
            return false;
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Counts the preview's last drawn shapes of one part
int32 FSOLHoloMenuSmoke::CountPart(const ESOLHoloPart part) const
{
    const SSOLHoloFrame* frame = mFrameWidget.IsValid() && mFrameWidget->GetFrame().IsValid()
        ? mFrameWidget->GetFrame().Get() : nullptr;
    if (frame == nullptr)
    {
        return 0;
    }
    int32 count = 0;
    const FSOLHoloFrameOutput& output = frame->GetLastOutput();
    for (int32 index = 0; index < output.ShapeCount; ++index)
    {
        count += output.Shapes[index].Part == part ? 1 : 0;
    }
    return count;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether every line and dot (not the fill) of the preview's last frame has this glow pad
bool FSOLHoloMenuSmoke::AllLinePads(const float padPx) const
{
    const SSOLHoloFrame* frame = mFrameWidget.IsValid() && mFrameWidget->GetFrame().IsValid()
        ? mFrameWidget->GetFrame().Get() : nullptr;
    if (frame == nullptr || frame->GetLastOutput().ShapeCount == 0)
    {
        return false;
    }
    const FSOLHoloFrameOutput& output = frame->GetLastOutput();
    for (int32 index = 0; index < output.ShapeCount; ++index)
    {
        if (output.Shapes[index].Part != ESOLHoloPart::Fill && output.Shapes[index].PadPx != padPx)
        {
            return false;
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns the menu material's scalar parameter, or -1 when there is no material
float FSOLHoloMenuSmoke::GetMenuParam(const TCHAR* name) const
{
    UMaterialInstanceDynamic* material = mHoloMenu.IsValid() ? mHoloMenu->GetMenuMaterial() : nullptr;
    return material != nullptr ? material->K2_GetScalarParameterValue(FName(name)) : -1.0f;
}

//////////////////////////////////////////////////////////////////////////
// Finds a body's icon for the radial steps (not the Sun, preferring one that is not the anchor)
bool FSOLHoloMenuSmoke::FindRadialBody(ASOLShipPawn& pawn)
{
    const USOLAnchorSubsystem* anchor = pawn.GetWorld()->GetSubsystem<USOLAnchorSubsystem>();
    const int32 anchorBody = anchor != nullptr ? anchor->GetAnchorIndex() : INDEX_NONE;
    bool bIsFound = false;
    for (const FSOLIconHit& hit : mInteraction->GetIconHits())
    {
        if (hit.Object.Kind != ESOLObjectKind::Body || hit.Object.Index == 0)
        {
            continue;
        }
        if (!bIsFound || (mRadialBody.Index == anchorBody && hit.Object.Index != anchorBody))
        {
            mRadialBody = hit.Object;
            mRadialBodyPx = hit.CenterPx;
            bIsFound = true;
        }
    }
    UE_LOG(LogSOL, Log, TEXT("HoloMenu: radial body %s (%d) at (%.0f, %.0f) of %d icons"), bIsFound ? TEXT("found")
        : TEXT("NOT found"), mRadialBody.Index, mRadialBodyPx.X, mRadialBodyPx.Y, mInteraction->GetIconHits().Num());
    return bIsFound;
}

//////////////////////////////////////////////////////////////////////////
// Presses Fire on the radial body's icon (its current centre, else where it was found)
void FSOLHoloMenuSmoke::PressOnRadialBody(ASOLShipPawn& pawn) const
{
    FVector2D iconPx = mRadialBodyPx;
    for (const FSOLIconHit& hit : mInteraction->GetIconHits())
    {
        iconPx = hit.Object == mRadialBody ? hit.CenterPx : iconPx;
    }
    pawn.HandleFlightCursorMoved(iconPx);
    mInteraction->UpdateHover(pawn.GetFlightCursorPx(), true);
    pawn.HandleFire(true);
}

//////////////////////////////////////////////////////////////////////////
// Moves the cursor onto an action's sector of the open radial menu (the middle of the sector ring) and updates the
// highlight; returns the sector, or INDEX_NONE
int32 FSOLHoloMenuSmoke::DragToRadialAction(ASOLShipPawn& pawn, const ESOLAction action) const
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
            return index;
        }
    }
    UE_LOG(LogSOL, Warning, TEXT("HoloMenu: the radial menu does not offer %s"), SOLActionTable::ActionLabel(action));
    return INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Logs the section 12 checks later parts fill in
void FSOLHoloMenuSmoke::LogStubs()
{
    // SDD 42 section 12: the step, what it will check and the part that adds it
    static const TCHAR* const STUBS[] = {
        TEXT("1 Ctrl+O: frame widget, Open after OPEN_S with its full outline, one open cue (42e)"),
        TEXT("2 Ctrl+O mid-open capture at ~0.1 s, frame fraction in (0, 1) (42e)"),
        TEXT("3 hover a row, then Down: at most two hover cues, brackets and pointer at the row (42e)"),
        TEXT("4 toggle a row: one select cue; Esc: input back at once, widget gone after CLOSE_S (42e)"),
        TEXT("6 F3: open, digit step (select cue), highlight move (hover cue), close (42f)"),
        TEXT("7 info / target / speed / date-and-anchor cards on the menu layer, 8 ticks each (42g)"),
        TEXT("8 map list menu: open, hover, select cues (42g)"),
        TEXT("9 strength 0 in the Overlay Menu, F3 panel and map list: instant open, flat look, still usable, cues "
            "requested (42e-42g; the radial's part runs here)"),
        TEXT("10 Settings: slider under the HUD effects slider, Apply writes once (covered by -SOLMenuSmoke; the "
            "cursor part runs here)"),
        TEXT("11 the Overlay list embedded in Settings has no frame widget (42e)"),
    };
    for (const TCHAR* stub : STUBS)
    {
        UE_LOG(LogSOL, Log, TEXT("HoloMenu STUB step %s"), stub);
    }
}

//////////////////////////////////////////////////////////////////////////
// Requests a window screenshot with the UI to the screenshot folder (Saved/Screenshots/<platform>)
void FSOLHoloMenuSmoke::TakeUiShot(const TCHAR* name)
{
    const FString path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir()
        / FString::Printf(TEXT("HoloMenu_%s.png"), name));
    FScreenshotRequest::RequestScreenshot(path, true, false);
    UE_LOG(LogSOL, Log, TEXT("HoloMenu: screenshot with UI %s"), *path);
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLHoloMenuSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("HoloMenu CHECK %-20s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"), *detail);
}
