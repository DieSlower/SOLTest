/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHoloMenuSmoke.h"

#include "Interaction/SOLInteractionSubsystem.h"
#include "Map/SOLJumpSubsystem.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLSettingsPageWidget.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "Ship/SOLShipPawn.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "UI/SOLFlightHud.h"
#include "UI/SOLHoloFrame.h"
#include "UI/SOLHoloFrameWidget.h"
#include "UI/SOLHoloMenuSubsystem.h"
#include "UI/SOLHologramCursorSubsystem.h"
#include "UI/SOLOverlayMenuWidget.h"
#include "UI/SOLSpeedPanelWidget.h"
#include "Universe/SOLAnchorSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SViewport.h"

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
        OverlayOpen,    // Steps 1-2: Ctrl+O: the open cue, the draw-in mid-way, then Open with outline, tabs, dividers
        OverlayHover,   // Step 3: the mouse onto a row, then Down: hover cues, brackets and pointer at the row
        OverlayTab,     // The mouse onto a tab: brackets only around it, one hover cue
        OverlaySelect,  // Step 4: a row toggled and back, PageDown, PageUp, the cycle key: one select cue each
        OverlayClose,   // Step 4: Esc: input back in the same frame, the close plays, the widget gone after CLOSE_S
        OverlayReverse, // Ctrl+O, Esc, Ctrl+O mid-close: the close reverses from its progress; Esc again
        OverlayFlat,    // Step 9: strength 0: whole and flat at once, a row toggle and the cues still work
        OverlayFlatClose,   // Esc at strength 0: the widget removed in the same frame
        OverlayStrong,  // Strength 2: the glow look (screenshot), then Esc
        OverlayEmbedded,    // Step 11: the Overlay list embedded in Settings has no frame widget and keeps its look
        SpeedOpen,      // Step 6: F3: the open cue, the draw-in mid-way, then Open with the outline and stepped divider
        SpeedDigit,     // Step 6: Up steps the highlighted digit (a select cue), Left moves the highlight (a hover cue)
        SpeedList,      // Tab and a wheel notch move the highlight into the list (hover cues, row mark), Enter (select)
        SpeedClose,     // Step 6: Esc: input back in the same frame, the close plays, the widget gone after CLOSE_S
        SpeedReverse,   // F3, Esc, F3 mid-close: the close reverses from its progress; Esc again
        SpeedFlat,      // Step 9: strength 0: whole and flat at once, a digit step and the cues still work
        SpeedFlatClose, // Esc at strength 0: the widget removed in the same frame
        SpeedStrong,    // Strength 2: the glow look (screenshot), then Esc; the cap restored
        Stubs,        // The section 12 checks later parts fill in
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
    constexpr int32 HOLOSMOKE_OVERLAY_HOVER_ROW = 3;       // The row the mouse moves onto (Down then takes the next)
    constexpr int32 HOLOSMOKE_OVERLAY_HOVER_TAB = 1;       // The tab the mouse moves onto (Profile 2)
    constexpr int32 HOLOSMOKE_OVERLAY_SELECT_KEYS = 5;     // Space, Space, PageDown, PageUp, the cycle key
    constexpr double HOLOSMOKE_OVERLAY_TIMEOUT_S = 3.0;    // Longest wait for Ctrl+O to open the menu
    constexpr double HOLOSMOKE_REVERSE_TIMEOUT_S = 6.0;    // Longest the reverse step runs
    constexpr double HOLOSMOKE_KEY_GAP_S = 0.25;           // Between two scripted presses (past every cue rate limit)
    constexpr double HOLOSMOKE_CAP_TOLERANCE = 1.0e-3;     // Relative tolerance of a cap read back from the ship
    constexpr float HOLOSMOKE_LIST_SCROLL_NOTCHES = 8.0f;  // Wheel notches over the F3 list (scrolls the row away)
    const FVector2D HOLOSMOKE_MOUSE_PARK(0.85, 0.5);       // Where the mouse waits, as a fraction of the viewport

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
    UE_LOG(LogSOL, Log, TEXT("HoloMenu: scripted hologram menu checks starting (parts 42c-42f)"));
    mPendingReleases.Reserve(4);
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
    for (const FKey& key : mPendingReleases)
    {
        mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Released, 0.0f));
    }
    mPendingReleases.Reset();
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
    case EHoloStep::OverlayOpen:
        // The mouse parked away from the menu first, so no hover lands on it while it opens
        mOpenRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Open);
        mHoverRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover);
        mOverlayOpenS = -1.0;
        mMidOpenFraction = -1.0f;
        mMidOpenOpacity = -1.0f;
        mIsMidOpenShot = false;
        ParkMouse(pawn);
        TapGameChord(EKeys::LeftControl, EKeys::O);
        break;
    case EHoloStep::OverlayHover:
        // Onto a row now, Down later in the step
        mHoverRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover);
        mIsSecondDrag = false;
        if (const USOLOverlayMenuWidget* menu = pawn.GetOverlayMenu())
        {
            SlateMouseMove(menu->GetRowScreenCenter(HOLOSMOKE_OVERLAY_HOVER_ROW));
        }
        break;
    case EHoloStep::OverlayTab:
        mHoverRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover);
        if (const USOLOverlayMenuWidget* menu = pawn.GetOverlayMenu())
        {
            SlateMouseMove(menu->GetTabScreenCenter(HOLOSMOKE_OVERLAY_HOVER_TAB));
        }
        break;
    case EHoloStep::OverlaySelect:
    {
        // The presses run in TickOverlaySelect, one every HOLOSMOKE_KEY_GAP_S
        const USOLOverlayMenuWidget* menu = pawn.GetOverlayMenu();
        mSelectRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Select);
        mSubStep = 0;
        mOverlayRow = menu != nullptr ? menu->GetHighlightedRow() : INDEX_NONE;
        mRowTextBefore = menu != nullptr ? menu->GetRowText(mOverlayRow) : FString();
        mRowTextToggled.Reset();
        mViewedAfterPageDown = INDEX_NONE;
        break;
    }
    case EHoloStep::OverlayClose:
    {
        // Esc, then at once (the same frame): the ship has the input back and the menu takes none while it closes
        SlateKey(EKeys::Escape, FModifierKeysState());
        const USOLOverlayMenuWidget* menu = pawn.GetOverlayMenu();
        const SSOLHoloFrame* overlayFrame = GetOverlayFrame(pawn);
        const bool bIsClosing = overlayFrame != nullptr
            && overlayFrame->GetAnim().Phase == ESOLHoloMenuPhase::Closing;
        const bool bIsUnfocused = menu != nullptr && !menu->IsFocusable() && !menu->HasAnyUserFocus();
        const bool bIsHitInvisible = menu != nullptr && menu->GetVisibility() == ESlateVisibility::HitTestInvisible;
        mIsInputBackAtEsc = !pawn.IsOverlayMenuOpen() && menu != nullptr && menu->IsClosing() && menu->IsInViewport()
            && bIsClosing && bIsUnfocused && bIsHitInvisible;
        mEscDetail = FString::Printf(TEXT("right after Esc: pawn menu %s, widget %s and %s, frame %s, %s, %s"),
            pawn.IsOverlayMenuOpen() ? TEXT("OPEN") : TEXT("closed"), menu != nullptr && menu->IsClosing()
            ? TEXT("closing") : TEXT("NOT closing"), menu != nullptr && menu->IsInViewport() ? TEXT("still drawn")
            : TEXT("REMOVED"), bIsClosing ? TEXT("Closing") : TEXT("NOT closing"), bIsUnfocused
            ? TEXT("unfocusable, no keyboard focus") : (menu != nullptr && menu->IsFocusable() ? TEXT("STILL FOCUSABLE")
            : TEXT("STILL FOCUSED")), bIsHitInvisible
            ? TEXT("hit-test invisible") : TEXT("STILL HIT-TESTABLE"));
        mIsMidCloseInViewport = false;
        mMidCloseOpacity = -1.0f;
        mMidCloseFraction = -1.0f;
        mMidClosePhase = ESOLHoloMenuPhase::Closed;
        break;
    }
    case EHoloStep::OverlayReverse:
    case EHoloStep::SpeedReverse:
        mOpenRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Open);
        mSubStep = 0;
        mReverseProgress = -1.0f;
        mReversePhase = ESOLHoloMenuPhase::Closed;
        mIsReverseInViewport = false;
        ParkMouse(pawn);
        if (static_cast<EHoloStep>(step) == EHoloStep::SpeedReverse)
        {
            TapGameKey(GetSpeedPanelKey());
        }
        else
        {
            TapGameChord(EKeys::LeftControl, EKeys::O);
        }
        break;
    case EHoloStep::OverlayFlat:
    {
        // Strength 0: opened, then (TickStep) a row toggled and back and the mouse onto another row
        SetStrength(0.0f);
        const SSOLHoloFrame* overlayFrame = GetOverlayFrame(pawn);
        mOpenRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Open);
        mHoverRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover);
        mSelectRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Select);
        mPaintsBefore = overlayFrame != nullptr ? overlayFrame->GetPaintCount() : 0;
        mFirstFlatFraction = -1.0f;
        mMidOpenOpacity = -1.0f;
        mSubStep = 0;
        mOverlayRow = INDEX_NONE;
        mRowTextBefore.Reset();
        mRowTextToggled.Reset();
        ParkMouse(pawn);
        TapGameChord(EKeys::LeftControl, EKeys::O);
        break;
    }
    case EHoloStep::OverlayFlatClose:
    {
        // At strength 0 the close is instant: the widget is off the viewport right after Esc
        SlateKey(EKeys::Escape, FModifierKeysState());
        const USOLOverlayMenuWidget* menu = pawn.GetOverlayMenu();
        mIsFlatClosedAtOnce = !pawn.IsOverlayMenuOpen() && menu != nullptr && !menu->IsInViewport();
        break;
    }
    case EHoloStep::OverlayStrong:
        SetStrength(SOL::Settings::HOLO_MENU_EFFECTS_MAX);
        mOverlayOpenS = -1.0;
        ParkMouse(pawn);
        TapGameChord(EKeys::LeftControl, EKeys::O);
        break;
    case EHoloStep::OverlayEmbedded:
        CheckEmbeddedOverlay();
        break;
    case EHoloStep::SpeedOpen:
        // The mouse parked away first; the cap is kept to restore it at the end of the F3 steps
        mOpenRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Open);
        mHoverRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover);
        mOverlayOpenS = -1.0;
        mMidOpenFraction = -1.0f;
        mMidOpenOpacity = -1.0f;
        mIsMidOpenShot = false;
        mSpeedCapRestoreMps = pawn.GetComposedControl().SpeedCapMps;
        ParkMouse(pawn);
        TapGameKey(GetSpeedPanelKey());
        break;
    case EHoloStep::SpeedDigit:
    case EHoloStep::SpeedList:
    {
        // The presses run in TickSpeedKeys, one every HOLOSMOKE_KEY_GAP_S
        const USOLSpeedPanelWidget* panel = pawn.GetSpeedPanel();
        mHoverRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover);
        mSelectRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Select);
        mSubStep = 0;
        mSubStepS = 0.0;
        mCapBeforeMps = pawn.GetComposedControl().SpeedCapMps;
        mCapAfterStepMps = -1.0;
        mSpeedItemBefore = panel != nullptr ? panel->GetHighlightedItem() : INDEX_NONE;
        mSpeedListBefore = panel != nullptr ? panel->GetListSelection() : INDEX_NONE;
        mSpeedRowHovers = -1;
        mSpeedRowPieces = -1;
        mSpeedRowDots = -1;
        mIsSpeedRowItem = false;
        mSpeedScrolledHovers = -1;
        mSpeedScrolledPieces = -1;
        mSpeedScrolledDots = -1;
        mSpeedScrollOffset = -1.0f;
        mIsSpeedScrolledItem = false;
        break;
    }
    case EHoloStep::SpeedClose:
    {
        // Esc, then at once (the same frame): the ship has the input back and the panel takes none while it closes
        SlateKey(EKeys::Escape, FModifierKeysState());
        const USOLSpeedPanelWidget* panel = pawn.GetSpeedPanel();
        const SSOLHoloFrame* speedFrame = GetSpeedFrame(pawn);
        const bool bIsClosing = speedFrame != nullptr && speedFrame->GetAnim().Phase == ESOLHoloMenuPhase::Closing;
        const bool bIsUnfocused = panel != nullptr && !panel->IsFocusable() && !panel->HasAnyUserFocus();
        const bool bIsHitInvisible = panel != nullptr
            && panel->GetVisibility() == ESlateVisibility::HitTestInvisible;
        mIsInputBackAtEsc = !pawn.IsSpeedPanelOpen() && panel != nullptr && panel->IsClosing()
            && panel->IsInViewport() && bIsClosing && bIsUnfocused && bIsHitInvisible;
        mEscDetail = FString::Printf(TEXT("right after Esc: pawn panel %s, widget %s and %s, frame %s, %s, %s"),
            pawn.IsSpeedPanelOpen() ? TEXT("OPEN") : TEXT("closed"), panel != nullptr && panel->IsClosing()
            ? TEXT("closing") : TEXT("NOT closing"), panel != nullptr && panel->IsInViewport() ? TEXT("still drawn")
            : TEXT("REMOVED"), bIsClosing ? TEXT("Closing") : TEXT("NOT closing"), bIsUnfocused
            ? TEXT("unfocusable, no keyboard focus") : TEXT("STILL FOCUSABLE OR FOCUSED"), bIsHitInvisible
            ? TEXT("hit-test invisible") : TEXT("STILL HIT-TESTABLE"));
        mIsMidCloseInViewport = false;
        mMidCloseOpacity = -1.0f;
        mMidCloseFraction = -1.0f;
        mMidClosePhase = ESOLHoloMenuPhase::Closed;
        break;
    }
    case EHoloStep::SpeedFlat:
    {
        // Strength 0: opened, then (TickStep) a digit stepped and the highlight moved
        SetStrength(0.0f);
        const SSOLHoloFrame* speedFrame = GetSpeedFrame(pawn);
        mOpenRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Open);
        mHoverRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover);
        mSelectRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Select);
        mPaintsBefore = speedFrame != nullptr ? speedFrame->GetPaintCount() : 0;
        mFirstFlatFraction = -1.0f;
        mMidOpenOpacity = -1.0f;
        mSubStep = 0;
        mCapBeforeMps = -1.0;
        mCapAfterStepMps = -1.0;
        ParkMouse(pawn);
        TapGameKey(GetSpeedPanelKey());
        break;
    }
    case EHoloStep::SpeedFlatClose:
    {
        // At strength 0 the close is instant: the widget is off the viewport right after Esc
        SlateKey(EKeys::Escape, FModifierKeysState());
        const USOLSpeedPanelWidget* panel = pawn.GetSpeedPanel();
        mIsFlatClosedAtOnce = !pawn.IsSpeedPanelOpen() && panel != nullptr && !panel->IsInViewport();
        break;
    }
    case EHoloStep::SpeedStrong:
        SetStrength(SOL::Settings::HOLO_MENU_EFFECTS_MAX);
        mOverlayOpenS = -1.0;
        ParkMouse(pawn);
        TapGameKey(GetSpeedPanelKey());
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
    case EHoloStep::OverlayOpen:
    {
        // The draw-in caught about half-way after the menu opened (with a screenshot), then Open
        const SSOLHoloFrame* overlayFrame = GetOverlayFrame(pawn);
        const USOLOverlayMenuWidget* menu = pawn.GetOverlayMenu();
        if (mOverlayOpenS < 0.0 && pawn.IsOverlayMenuOpen())
        {
            mOverlayOpenS = elapsedS;
        }
        if (overlayFrame != nullptr && menu != nullptr && mOverlayOpenS >= 0.0 && mMidOpenFraction < 0.0f
            && elapsedS >= mOverlayOpenS + HOLOSMOKE_MID_OPEN_S)
        {
            mMidOpenFraction = overlayFrame->GetLastLook().FrameFraction;
            mMidOpenOpacity = menu->GetContentOpacity();
            TakeUiShot(TEXT("OverlayMidOpen"));
            mIsMidOpenShot = true;
        }
        return (mOverlayOpenS >= 0.0 && elapsedS >= mOverlayOpenS + HOLOSMOKE_OPENED_S)
            || elapsedS >= HOLOSMOKE_OVERLAY_TIMEOUT_S;
    }
    case EHoloStep::OverlayHover:
        if (!mIsSecondDrag && elapsedS >= HOLOSMOKE_KEY_GAP_S)
        {
            SlateKey(EKeys::Down, FModifierKeysState());
            mIsSecondDrag = true;
        }
        return ShotThenDone(TEXT("Overlay_S1"), mIsSecondDrag && elapsedS >= HOLOSMOKE_KEY_GAP_S
            + HOLOSMOKE_SETTLED_S);
    case EHoloStep::OverlayTab:
        return ShotThenDone(TEXT("Overlay_Tab"), elapsedS >= HOLOSMOKE_SETTLED_S);
    case EHoloStep::OverlaySelect:
        TickOverlaySelect(pawn, elapsedS);
        return mSubStep >= HOLOSMOKE_OVERLAY_SELECT_KEYS
            && elapsedS >= HOLOSMOKE_OVERLAY_SELECT_KEYS * HOLOSMOKE_KEY_GAP_S + HOLOSMOKE_STEP_S;
    case EHoloStep::OverlayClose:
    {
        // The close caught part-way: still on the viewport, closing, with a screenshot
        const SSOLHoloFrame* overlayFrame = GetOverlayFrame(pawn);
        const USOLOverlayMenuWidget* menu = pawn.GetOverlayMenu();
        if (overlayFrame != nullptr && menu != nullptr && mMidCloseFraction < 0.0f
            && elapsedS >= HOLOSMOKE_MID_CLOSE_S)
        {
            mMidCloseFraction = overlayFrame->GetLastLook().FrameFraction;
            mMidClosePhase = overlayFrame->GetAnim().Phase;
            mMidCloseOpacity = menu->GetContentOpacity();
            mIsMidCloseInViewport = menu->IsInViewport();
            TakeUiShot(TEXT("OverlayClosing"));
        }
        return elapsedS >= HOLOSMOKE_CLOSED_S;
    }
    case EHoloStep::OverlayReverse:
    case EHoloStep::SpeedReverse:
        return TickReverse(pawn, static_cast<EHoloStep>(step) == EHoloStep::SpeedReverse, elapsedS);
    case EHoloStep::OverlayFlat:
    {
        // The first paint after the open, then a row toggled and back, then the mouse onto another row
        const SSOLHoloFrame* overlayFrame = GetOverlayFrame(pawn);
        const USOLOverlayMenuWidget* menu = pawn.GetOverlayMenu();
        if (overlayFrame != nullptr && menu != nullptr && pawn.IsOverlayMenuOpen() && mFirstFlatFraction < 0.0f
            && overlayFrame->GetPaintCount() != mPaintsBefore)
        {
            mFirstFlatFraction = overlayFrame->GetLastLook().FrameFraction;
            mMidOpenOpacity = menu->GetContentOpacity();
            mSubStepS = elapsedS;
        }
        if (menu == nullptr || mFirstFlatFraction < 0.0f)
        {
            return elapsedS >= HOLOSMOKE_OVERLAY_TIMEOUT_S;
        }
        if (mSubStep < 3 && elapsedS >= mSubStepS + HOLOSMOKE_KEY_GAP_S)
        {
            if (mSubStep == 0)
            {
                mOverlayRow = menu->GetHighlightedRow();
                mRowTextBefore = menu->GetRowText(mOverlayRow);
                SlateKey(EKeys::SpaceBar, FModifierKeysState());
            }
            else if (mSubStep == 1)
            {
                mRowTextToggled = menu->GetRowText(mOverlayRow);
                SlateKey(EKeys::SpaceBar, FModifierKeysState());
            }
            else
            {
                SlateMouseMove(menu->GetRowScreenCenter(HOLOSMOKE_OVERLAY_HOVER_ROW));
            }
            ++mSubStep;
            mSubStepS = elapsedS;
        }
        return mSubStep >= 3 && ShotThenDone(TEXT("Overlay_S0"), elapsedS >= mSubStepS + HOLOSMOKE_SETTLED_S);
    }
    case EHoloStep::OverlayStrong:
        if (mOverlayOpenS < 0.0 && pawn.IsOverlayMenuOpen())
        {
            mOverlayOpenS = elapsedS;
        }
        return ShotThenDone(TEXT("Overlay_S2"), (mOverlayOpenS >= 0.0 && elapsedS >= mOverlayOpenS
            + HOLOSMOKE_SETTLED_S) || elapsedS >= HOLOSMOKE_OVERLAY_TIMEOUT_S);
    case EHoloStep::SpeedOpen:
    {
        // The draw-in caught about half-way after the panel opened (with a screenshot), then Open
        const SSOLHoloFrame* speedFrame = GetSpeedFrame(pawn);
        const USOLSpeedPanelWidget* panel = pawn.GetSpeedPanel();
        if (mOverlayOpenS < 0.0 && pawn.IsSpeedPanelOpen())
        {
            mOverlayOpenS = elapsedS;
        }
        if (speedFrame != nullptr && panel != nullptr && mOverlayOpenS >= 0.0 && mMidOpenFraction < 0.0f
            && elapsedS >= mOverlayOpenS + HOLOSMOKE_MID_OPEN_S)
        {
            mMidOpenFraction = speedFrame->GetLastLook().FrameFraction;
            mMidOpenOpacity = panel->GetContentOpacity();
            TakeUiShot(TEXT("SpeedMidOpen"));
            mIsMidOpenShot = true;
        }
        return ShotThenDone(TEXT("Speed_S1"), (mOverlayOpenS >= 0.0 && elapsedS >= mOverlayOpenS
            + HOLOSMOKE_SETTLED_S) || elapsedS >= HOLOSMOKE_OVERLAY_TIMEOUT_S);
    }
    case EHoloStep::SpeedDigit:
    case EHoloStep::SpeedList:
        return TickSpeedKeys(pawn, static_cast<EHoloStep>(step) == EHoloStep::SpeedList, elapsedS);
    case EHoloStep::SpeedClose:
    {
        // The close caught part-way: still on the viewport, closing, with a screenshot
        const SSOLHoloFrame* speedFrame = GetSpeedFrame(pawn);
        const USOLSpeedPanelWidget* panel = pawn.GetSpeedPanel();
        if (speedFrame != nullptr && panel != nullptr && mMidCloseFraction < 0.0f
            && elapsedS >= HOLOSMOKE_MID_CLOSE_S)
        {
            mMidCloseFraction = speedFrame->GetLastLook().FrameFraction;
            mMidClosePhase = speedFrame->GetAnim().Phase;
            mMidCloseOpacity = panel->GetContentOpacity();
            mIsMidCloseInViewport = panel->IsInViewport();
            TakeUiShot(TEXT("SpeedClosing"));
        }
        return elapsedS >= HOLOSMOKE_CLOSED_S;
    }
    case EHoloStep::SpeedFlat:
    {
        // The first paint after the open, then a digit stepped up, then the highlight moved left
        const SSOLHoloFrame* speedFrame = GetSpeedFrame(pawn);
        const USOLSpeedPanelWidget* panel = pawn.GetSpeedPanel();
        if (speedFrame != nullptr && panel != nullptr && pawn.IsSpeedPanelOpen() && mFirstFlatFraction < 0.0f
            && speedFrame->GetPaintCount() != mPaintsBefore)
        {
            mFirstFlatFraction = speedFrame->GetLastLook().FrameFraction;
            mMidOpenOpacity = panel->GetContentOpacity();
            mSubStepS = elapsedS;
        }
        if (panel == nullptr || mFirstFlatFraction < 0.0f)
        {
            return elapsedS >= HOLOSMOKE_OVERLAY_TIMEOUT_S;
        }
        if (mSubStep < 2 && elapsedS >= mSubStepS + HOLOSMOKE_KEY_GAP_S)
        {
            if (mSubStep == 0)
            {
                mCapBeforeMps = pawn.GetComposedControl().SpeedCapMps;
                SlateKey(EKeys::Up, FModifierKeysState());
            }
            else
            {
                mCapAfterStepMps = pawn.GetComposedControl().SpeedCapMps;
                SlateKey(EKeys::Left, FModifierKeysState());
            }
            ++mSubStep;
            mSubStepS = elapsedS;
        }
        return mSubStep >= 2 && ShotThenDone(TEXT("Speed_S0"), elapsedS >= mSubStepS + HOLOSMOKE_SETTLED_S);
    }
    case EHoloStep::SpeedStrong:
        if (mOverlayOpenS < 0.0 && pawn.IsSpeedPanelOpen())
        {
            mOverlayOpenS = elapsedS;
        }
        return ShotThenDone(TEXT("Speed_S2"), (mOverlayOpenS >= 0.0 && elapsedS >= mOverlayOpenS
            + HOLOSMOKE_SETTLED_S) || elapsedS >= HOLOSMOKE_OVERLAY_TIMEOUT_S);
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
    case EHoloStep::OverlayOpen:
    {
        // Steps 1-2: one open cue, no hover cue while it opened, the draw-in mid-way, then the full option (c) frame
        const SSOLHoloFrame* overlayFrame = GetOverlayFrame(pawn);
        const USOLOverlayMenuWidget* menu = pawn.GetOverlayMenu();
        const int32 opens = holoMenu->GetCueRequestCount(ESOLHoloCue::Open) - mOpenRequestsBefore;
        const int32 hovers = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover) - mHoverRequestsBefore;
        const bool bIsOpen = overlayFrame != nullptr && overlayFrame->GetAnim().Phase == ESOLHoloMenuPhase::Open;
        const int32 outline = CountPart(ESOLHoloPart::Outline, overlayFrame);
        const int32 fullOutline = overlayFrame != nullptr ? FullOutlineCount(*overlayFrame) : -1;
        const int32 tabs = menu != nullptr ? menu->GetTabCount() : 0;
        const int32 tabPieces = CountPart(ESOLHoloPart::Tab, overlayFrame);
        const int32 expectedTabPieces = tabs > 0 ? TAB_MAX_SEGMENTS + 3 * (tabs - 1) : -1;
        Check(TEXT("o-overlay-open"), pawn.IsOverlayMenuOpen() && menu != nullptr && menu->GetHoloFrame() != nullptr
            && bIsOpen && opens == 1 && hovers == 0 && outline == fullOutline && outline > 0
            && CountPart(ESOLHoloPart::GapDot, overlayFrame) > 0 && tabPieces == expectedTabPieces
            && CountPart(ESOLHoloPart::TabDot, overlayFrame) == 1
            && CountPart(ESOLHoloPart::ColumnDivider, overlayFrame) > 0
            && CountPart(ESOLHoloPart::BrokenDivider, overlayFrame) > 0
            && overlayFrame->GetLastPaintedBackingCount() == 1 && overlayFrame->GetMaterial() == menuMaterial
            && menu->GetContentOpacity() == 1.0f && IsFrameOutputFresh(overlayFrame),
            FString::Printf(TEXT("menu %s, phase %s, %d open / %d hover cue requests, outline %d of %d pieces, %d gap "
                "dots, %d tab pieces (want %d) and %d tab dot, %d column dashes, %d hint divider pieces, %d backing, "
                "material %s, content opacity %.2f"), pawn.IsOverlayMenuOpen() ? TEXT("open") : TEXT("CLOSED"),
                bIsOpen ? TEXT("Open") : TEXT("NOT open"), opens, hovers, outline, fullOutline,
                CountPart(ESOLHoloPart::GapDot, overlayFrame), tabPieces, expectedTabPieces,
                CountPart(ESOLHoloPart::TabDot, overlayFrame), CountPart(ESOLHoloPart::ColumnDivider, overlayFrame),
                CountPart(ESOLHoloPart::BrokenDivider, overlayFrame), overlayFrame != nullptr
                ? overlayFrame->GetLastPaintedBackingCount() : -1, overlayFrame != nullptr
                && overlayFrame->GetMaterial() == menuMaterial ? TEXT("menu MID") : TEXT("OTHER"),
                menu != nullptr ? menu->GetContentOpacity() : -1.0f));
        Check(TEXT("o-overlay-mid-open"), mIsMidOpenShot && mMidOpenFraction > 0.0f && mMidOpenFraction < 1.0f
            && mMidOpenOpacity >= 0.0f && mMidOpenOpacity < 1.0f, FString::Printf(TEXT("frame fraction %.3f, content "
                "opacity %.3f at %.2f s after the open"), mMidOpenFraction, mMidOpenOpacity, HOLOSMOKE_MID_OPEN_S));
        break;
    }
    case EHoloStep::OverlayHover:
    {
        // Step 3: the mouse onto a row and Down: at most two hover cues, brackets and a pointer at the highlighted row
        const SSOLHoloFrame* overlayFrame = GetOverlayFrame(pawn);
        const USOLOverlayMenuWidget* menu = pawn.GetOverlayMenu();
        const int32 hovers = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover) - mHoverRequestsBefore;
        const int32 highlighted = menu != nullptr ? menu->GetHighlightedRow() : INDEX_NONE;
        const int32 hoveredItem = overlayFrame != nullptr ? overlayFrame->GetAnim().HoveredItem : INDEX_NONE;
        const FSOLHoloFrameSpec* spec = overlayFrame != nullptr ? &overlayFrame->GetLastPixelSpec() : nullptr;
        const bool bIsRowSide = spec != nullptr && spec->HoverItemRect.bIsValid
            && spec->HoverItemRect.Min.X > spec->TabDividerX;
        Check(TEXT("o-overlay-hover"), hovers >= 1 && hovers <= 2 && highlighted == HOLOSMOKE_OVERLAY_HOVER_ROW + 1
            && hoveredItem == highlighted && CountPart(ESOLHoloPart::Hover, overlayFrame) == HOVER_MAX_SEGMENTS
            && CountPart(ESOLHoloPart::HoverDot, overlayFrame) == 1 && bIsRowSide,
            FString::Printf(TEXT("mouse onto row %d, then Down: %d hover cue requests, highlighted row %d, hovered "
                "item %d, %d hover pieces and %d dot, mark %s the column divider"), HOLOSMOKE_OVERLAY_HOVER_ROW,
                hovers, highlighted, hoveredItem, CountPart(ESOLHoloPart::Hover, overlayFrame),
                CountPart(ESOLHoloPart::HoverDot, overlayFrame), bIsRowSide ? TEXT("right of") : TEXT("NOT right of")));
        break;
    }
    case EHoloStep::OverlayTab:
    {
        // A hovered tab gets the brackets only (BracketsOnly), left of the column divider, with one hover cue
        const SSOLHoloFrame* overlayFrame = GetOverlayFrame(pawn);
        const USOLOverlayMenuWidget* menu = pawn.GetOverlayMenu();
        const int32 hovers = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover) - mHoverRequestsBefore;
        const int32 hoveredItem = overlayFrame != nullptr ? overlayFrame->GetAnim().HoveredItem : INDEX_NONE;
        const FSOLHoloFrameSpec* spec = overlayFrame != nullptr ? &overlayFrame->GetLastPixelSpec() : nullptr;
        const bool bIsTabSide = spec != nullptr && spec->HoverItemRect.bIsValid
            && spec->HoverItemRect.Max.X < spec->TabDividerX;
        const int32 bracketPieces = HOVER_MAX_SEGMENTS - 2;
        Check(TEXT("o-overlay-tab-hover"), menu != nullptr && menu->GetHoveredTab() == HOLOSMOKE_OVERLAY_HOVER_TAB
            && hoveredItem == USOLOverlayMenuWidget::ROW_WIDGET_COUNT + HOLOSMOKE_OVERLAY_HOVER_TAB && hovers == 1
            && CountPart(ESOLHoloPart::Hover, overlayFrame) == bracketPieces
            && CountPart(ESOLHoloPart::HoverDot, overlayFrame) == 0 && bIsTabSide,
            FString::Printf(TEXT("mouse onto tab %d: hovered tab %d, item %d, %d hover cue requests, %d hover pieces "
                "and %d dots, mark %s the column divider"), HOLOSMOKE_OVERLAY_HOVER_TAB + 1, menu != nullptr
                ? menu->GetHoveredTab() + 1 : 0, hoveredItem, hovers, CountPart(ESOLHoloPart::Hover, overlayFrame),
                CountPart(ESOLHoloPart::HoverDot, overlayFrame), bIsTabSide ? TEXT("left of") : TEXT("NOT left of")));
        break;
    }
    case EHoloStep::OverlaySelect:
    {
        // Step 4: a row toggle (twice), a tab switch (twice) and the cycle key each asked one select cue
        const USOLOverlayMenuWidget* menu = pawn.GetOverlayMenu();
        const int32 selects = holoMenu->GetCueRequestCount(ESOLHoloCue::Select) - mSelectRequestsBefore;
        const FString rowText = menu != nullptr ? menu->GetRowText(mOverlayRow) : FString();
        const int32 viewed = menu != nullptr ? menu->GetViewedProfile() : INDEX_NONE;
        Check(TEXT("o-overlay-select"), selects == HOLOSMOKE_OVERLAY_SELECT_KEYS && !mRowTextToggled.IsEmpty()
            && mRowTextToggled != mRowTextBefore && rowText == mRowTextBefore && mViewedAfterPageDown == 1
            && viewed == 0, FString::Printf(TEXT("%d select cue requests (want %d); row %d '%s' -> '%s' -> '%s'; "
                "PageDown viewed Profile %d, then Profile %d"), selects, HOLOSMOKE_OVERLAY_SELECT_KEYS, mOverlayRow,
                *mRowTextBefore.TrimStart(), *mRowTextToggled.TrimStart(), *rowText.TrimStart(),
                mViewedAfterPageDown + 1, viewed + 1));
        break;
    }
    case EHoloStep::OverlayClose:
    {
        // Step 4: input back at once, the close drawn part-way, then the widget gone
        const SSOLHoloFrame* overlayFrame = GetOverlayFrame(pawn);
        const USOLOverlayMenuWidget* menu = pawn.GetOverlayMenu();
        Check(TEXT("o-overlay-esc-input"), mIsInputBackAtEsc, mEscDetail);
        const bool bIsGone = menu != nullptr && !menu->IsInViewport();
        const bool bIsClosed = overlayFrame == nullptr || overlayFrame->GetAnim().Phase == ESOLHoloMenuPhase::Closed;
        Check(TEXT("o-overlay-close"), mIsMidCloseInViewport && mMidClosePhase == ESOLHoloMenuPhase::Closing
            && mMidCloseFraction > 0.0f && mMidCloseFraction < 1.0f && mMidCloseOpacity >= 0.0f
            && mMidCloseOpacity < 1.0f && bIsGone && bIsClosed, FString::Printf(TEXT("mid-close: %s, phase %d, "
                "draw-in %.3f, content opacity %.3f; after %.2f s: widget %s, frame %s"), mIsMidCloseInViewport
                ? TEXT("drawn") : TEXT("NOT drawn"), static_cast<int32>(mMidClosePhase), mMidCloseFraction,
                mMidCloseOpacity, HOLOSMOKE_CLOSED_S, bIsGone ? TEXT("removed") : TEXT("STILL ON THE VIEWPORT"),
                bIsClosed ? TEXT("Closed") : TEXT("NOT closed")));
        break;
    }
    case EHoloStep::OverlayReverse:
    {
        // Ctrl+O during the close reverses it from its progress (no fresh draw-in, the widget never left)
        const USOLOverlayMenuWidget* menu = pawn.GetOverlayMenu();
        const int32 opens = holoMenu->GetCueRequestCount(ESOLHoloCue::Open) - mOpenRequestsBefore;
        const bool bIsGone = menu != nullptr && !menu->IsInViewport();
        Check(TEXT("o-overlay-reverse"), mSubStep >= 5 && mReversePhase == ESOLHoloMenuPhase::Opening
            && mReverseProgress > 0.0f && mReverseProgress < 1.0f && mIsReverseInViewport && opens == 2 && bIsGone
            && !pawn.IsOverlayMenuOpen(), FString::Printf(TEXT("reopened mid-close: phase %d from progress %.3f, "
                "widget %s; %d open cue requests; closed again: widget %s"), static_cast<int32>(mReversePhase),
                mReverseProgress, mIsReverseInViewport ? TEXT("kept") : TEXT("RE-ADDED"), opens, bIsGone
                ? TEXT("removed") : TEXT("STILL ON THE VIEWPORT")));
        break;
    }
    case EHoloStep::SpeedReverse:
    {
        // F3 during the close reverses it from its progress (no fresh draw-in, the widget never left)
        const USOLSpeedPanelWidget* panel = pawn.GetSpeedPanel();
        const int32 opens = holoMenu->GetCueRequestCount(ESOLHoloCue::Open) - mOpenRequestsBefore;
        const bool bIsGone = panel != nullptr && !panel->IsInViewport();
        Check(TEXT("f-speed-reverse"), mSubStep >= 5 && mReversePhase == ESOLHoloMenuPhase::Opening
            && mReverseProgress > 0.0f && mReverseProgress < 1.0f && mIsReverseInViewport && opens == 2 && bIsGone
            && !pawn.IsSpeedPanelOpen(), FString::Printf(TEXT("reopened mid-close: phase %d from progress %.3f, "
                "widget %s; %d open cue requests; closed again: widget %s"), static_cast<int32>(mReversePhase),
                mReverseProgress, mIsReverseInViewport ? TEXT("kept") : TEXT("RE-ADDED"), opens, bIsGone
                ? TEXT("removed") : TEXT("STILL ON THE VIEWPORT")));
        break;
    }
    case EHoloStep::OverlayFlat:
    {
        // Step 9, the Overlay Menu at strength 0 (D18): whole and flat at once, still usable; the cues asked (D21)
        const SSOLHoloFrame* overlayFrame = GetOverlayFrame(pawn);
        const USOLOverlayMenuWidget* menu = pawn.GetOverlayMenu();
        const int32 opens = holoMenu->GetCueRequestCount(ESOLHoloCue::Open) - mOpenRequestsBefore;
        const int32 hovers = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover) - mHoverRequestsBefore;
        const int32 selects = holoMenu->GetCueRequestCount(ESOLHoloCue::Select) - mSelectRequestsBefore;
        const FSOLHoloFrameOutput* output = overlayFrame != nullptr ? &overlayFrame->GetLastOutput() : nullptr;
        const float alpha = output != nullptr && output->bHasBacking ? output->Backing.Alpha : -1.0f;
        const FString rowText = menu != nullptr ? menu->GetRowText(mOverlayRow) : FString();
        const int32 fullOutline = overlayFrame != nullptr ? FullOutlineCount(*overlayFrame) : -1;
        Check(TEXT("o-strength0-open"), mFirstFlatFraction == 1.0f && mMidOpenOpacity == 1.0f && opens == 1,
            FString::Printf(TEXT("first paint after Ctrl+O: frame fraction %.3f, content opacity %.3f; %d open cue "
                "requests (D21)"), mFirstFlatFraction, mMidOpenOpacity, opens));
        Check(TEXT("o-strength0-flat"), overlayFrame != nullptr && overlayFrame->GetLastStrength() == 0.0f
            && CountPart(ESOLHoloPart::Sweep, overlayFrame) == 0 && alpha == FLAT_PANEL_ALPHA
            && AllLinePads(FRAME_FLAT_PAD_PX, overlayFrame)
            && CountPart(ESOLHoloPart::Outline, overlayFrame) == fullOutline
            && CountPart(ESOLHoloPart::Hover, overlayFrame) == HOVER_MAX_SEGMENTS
            && CountPart(ESOLHoloPart::Tab, overlayFrame) > 0,
            FString::Printf(TEXT("strength %.2f, %d sweeps, backing alpha %.2f, flat pads %s, outline %d of %d pieces, "
                "%d hover pieces, %d tab pieces"), overlayFrame != nullptr ? overlayFrame->GetLastStrength() : -1.0f,
                CountPart(ESOLHoloPart::Sweep, overlayFrame), alpha, AllLinePads(FRAME_FLAT_PAD_PX, overlayFrame)
                ? TEXT("yes") : TEXT("NO"), CountPart(ESOLHoloPart::Outline, overlayFrame), fullOutline,
                CountPart(ESOLHoloPart::Hover, overlayFrame), CountPart(ESOLHoloPart::Tab, overlayFrame)));
        // Hover cues: none for the open (Open at once at strength 0, SDD 5 "no hover cue on open"), one for the mouse
        // onto another row (none when that row already had the highlight)
        const int32 wantHovers = mOverlayRow != HOLOSMOKE_OVERLAY_HOVER_ROW ? 1 : 0;
        Check(TEXT("o-strength0-usable"), !mRowTextToggled.IsEmpty() && mRowTextToggled != mRowTextBefore
            && rowText == mRowTextBefore && selects == 2 && hovers == wantHovers, FString::Printf(TEXT("row %d '%s' "
                "-> '%s' -> '%s'; %d select and %d hover cue requests (D21; want 2 and %d)"), mOverlayRow,
                *mRowTextBefore.TrimStart(), *mRowTextToggled.TrimStart(), *rowText.TrimStart(), selects, hovers,
                wantHovers));
        break;
    }
    case EHoloStep::OverlayFlatClose:
        Check(TEXT("o-strength0-close"), mIsFlatClosedAtOnce, FString::Printf(TEXT("Esc at strength 0: widget %s "
            "right after it"), mIsFlatClosedAtOnce ? TEXT("removed") : TEXT("STILL ON THE VIEWPORT")));
        break;
    case EHoloStep::OverlayStrong:
    {
        const SSOLHoloFrame* overlayFrame = GetOverlayFrame(pawn);
        const FSOLHoloFrameOutput* output = overlayFrame != nullptr ? &overlayFrame->GetLastOutput() : nullptr;
        const float alpha = output != nullptr && output->bHasBacking ? output->Backing.Alpha : -1.0f;
        Check(TEXT("o-strength2"), overlayFrame != nullptr
            && overlayFrame->GetLastStrength() == SOL::Settings::HOLO_MENU_EFFECTS_MAX
            && CountPart(ESOLHoloPart::Sweep, overlayFrame) == 1
            && AllLinePads(SOL::Hologram::HUD_GLOW_PAD_PX, overlayFrame)
            && FMath::IsNearlyEqual(alpha, 2.0f * BACKDROP_ALPHA_PER_UNIT),
            FString::Printf(TEXT("strength %.2f, %d sweep, glow pads %s, backing alpha %.2f"), overlayFrame != nullptr
                ? overlayFrame->GetLastStrength() : -1.0f, CountPart(ESOLHoloPart::Sweep, overlayFrame),
                AllLinePads(SOL::Hologram::HUD_GLOW_PAD_PX, overlayFrame) ? TEXT("yes") : TEXT("NO"), alpha));
        SlateKey(EKeys::Escape, FModifierKeysState());
        SetStrength(SOL::Settings::HOLO_MENU_EFFECTS_DEFAULT);
        break;
    }
    case EHoloStep::SpeedOpen:
    {
        // Step 6: one open cue, no hover cue while it opened, the draw-in mid-way, then the full option (c) frame with
        // the stepped divider (no column divider, no tabs) and the centre-tick mark on the highlighted digit
        const SSOLHoloFrame* speedFrame = GetSpeedFrame(pawn);
        const USOLSpeedPanelWidget* panel = pawn.GetSpeedPanel();
        const int32 opens = holoMenu->GetCueRequestCount(ESOLHoloCue::Open) - mOpenRequestsBefore;
        const int32 hovers = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover) - mHoverRequestsBefore;
        const bool bIsOpen = speedFrame != nullptr && speedFrame->GetAnim().Phase == ESOLHoloMenuPhase::Open;
        const int32 outline = CountPart(ESOLHoloPart::Outline, speedFrame);
        const int32 fullOutline = speedFrame != nullptr ? FullOutlineCount(*speedFrame) : -1;
        const float stepPx = speedFrame != nullptr ? speedFrame->GetLastPixelSpec().BrokenStepPx : -1.0f;
        const int32 hoveredItem = speedFrame != nullptr ? speedFrame->GetAnim().HoveredItem : INDEX_NONE;
        Check(TEXT("f-speed-open"), pawn.IsSpeedPanelOpen() && panel != nullptr && panel->GetHoloFrame() != nullptr
            && bIsOpen && opens == 1 && hovers == 0 && outline == fullOutline && outline > 0
            && CountPart(ESOLHoloPart::GapDot, speedFrame) > 0
            && CountPart(ESOLHoloPart::BrokenDivider, speedFrame) > 0 && stepPx > 0.0f
            && CountPart(ESOLHoloPart::ColumnDivider, speedFrame) == 0 && CountPart(ESOLHoloPart::Tab, speedFrame) == 0
            && speedFrame->GetLastPaintedBackingCount() == 1 && speedFrame->GetMaterial() == menuMaterial
            && panel->GetContentOpacity() == 1.0f && IsFrameOutputFresh(speedFrame),
            FString::Printf(TEXT("panel %s, phase %s, %d open / %d hover cue requests, outline %d of %d pieces, %d gap "
                "dots, %d divider pieces (step %.1f px), %d column dashes, %d tab pieces, %d backing, material %s, "
                "content opacity %.2f"), pawn.IsSpeedPanelOpen() ? TEXT("open") : TEXT("CLOSED"), bIsOpen
                ? TEXT("Open") : TEXT("NOT open"), opens, hovers, outline, fullOutline,
                CountPart(ESOLHoloPart::GapDot, speedFrame), CountPart(ESOLHoloPart::BrokenDivider, speedFrame),
                stepPx, CountPart(ESOLHoloPart::ColumnDivider, speedFrame), CountPart(ESOLHoloPart::Tab, speedFrame),
                speedFrame != nullptr ? speedFrame->GetLastPaintedBackingCount() : -1, speedFrame != nullptr
                && speedFrame->GetMaterial() == menuMaterial ? TEXT("menu MID") : TEXT("OTHER"), panel != nullptr
                ? panel->GetContentOpacity() : -1.0f));
        Check(TEXT("f-speed-mid-open"), mIsMidOpenShot && mMidOpenFraction > 0.0f && mMidOpenFraction < 1.0f
            && mMidOpenOpacity >= 0.0f && mMidOpenOpacity < 1.0f, FString::Printf(TEXT("frame fraction %.3f, content "
                "opacity %.3f at %.2f s after the open"), mMidOpenFraction, mMidOpenOpacity, HOLOSMOKE_MID_OPEN_S));
        Check(TEXT("f-speed-digit-mark"), panel != nullptr && hoveredItem == panel->GetHighlightedItem()
            && CountPart(ESOLHoloPart::Hover, speedFrame) == HOVER_MAX_SEGMENTS
            && CountPart(ESOLHoloPart::HoverDot, speedFrame) == 0, FString::Printf(TEXT("hovered item %d (highlight "
                "%d), %d hover pieces and %d dots (brackets and centre ticks)"), hoveredItem, panel != nullptr
                ? panel->GetHighlightedItem() : -1, CountPart(ESOLHoloPart::Hover, speedFrame),
                CountPart(ESOLHoloPart::HoverDot, speedFrame)));
        break;
    }
    case EHoloStep::SpeedDigit:
    {
        // Step 6: Up applied a cap (one select cue), Left moved the highlight (one hover cue, the mark followed it)
        const SSOLHoloFrame* speedFrame = GetSpeedFrame(pawn);
        const USOLSpeedPanelWidget* panel = pawn.GetSpeedPanel();
        const int32 selects = holoMenu->GetCueRequestCount(ESOLHoloCue::Select) - mSelectRequestsBefore;
        const int32 hovers = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover) - mHoverRequestsBefore;
        const int32 item = panel != nullptr ? panel->GetHighlightedItem() : INDEX_NONE;
        const int32 hoveredItem = speedFrame != nullptr ? speedFrame->GetAnim().HoveredItem : INDEX_NONE;
        Check(TEXT("f-speed-digit"), mCapAfterStepMps > 0.0 && mCapAfterStepMps != mCapBeforeMps && selects == 1
            && hovers == 1 && item != mSpeedItemBefore && hoveredItem == item
            && CountPart(ESOLHoloPart::Hover, speedFrame) == HOVER_MAX_SEGMENTS
            && CountPart(ESOLHoloPart::HoverDot, speedFrame) == 0, FString::Printf(TEXT("Up: cap %.3f -> %.3f m/s, %d "
                "select cue requests; Left: item %d -> %d, hovered %d, %d hover cue requests, %d hover pieces and %d "
                "dots"), mCapBeforeMps, mCapAfterStepMps, selects, mSpeedItemBefore, item, hoveredItem, hovers,
                CountPart(ESOLHoloPart::Hover, speedFrame), CountPart(ESOLHoloPart::HoverDot, speedFrame)));
        break;
    }
    case EHoloStep::SpeedList:
    {
        // Tab and a wheel notch: two hover cues, the row mark (brackets and pointer) on the next row; Enter applied
        // that row's speed (one select cue) and handed the highlight back to the steppers
        const USOLSpeedPanelWidget* panel = pawn.GetSpeedPanel();
        const int32 selects = holoMenu->GetCueRequestCount(ESOLHoloCue::Select) - mSelectRequestsBefore;
        const int32 row = mSpeedListBefore + 1;
        const double listed = panel != nullptr ? panel->GetListedBodySpeedMps(row) : 0.0;
        const double cap = pawn.GetComposedControl().SpeedCapMps;
        Check(TEXT("f-speed-list"), mSpeedRowHovers == 2 && mIsSpeedRowItem && mSpeedRowPieces == HOVER_MAX_SEGMENTS
            && mSpeedRowDots == 1 && selects == 1 && panel != nullptr && !panel->IsListFocused()
            && panel->GetListSelection() == row && listed > 0.0
            && FMath::Abs(cap - listed) <= HOLOSMOKE_CAP_TOLERANCE * listed, FString::Printf(TEXT("Tab, wheel down: "
                "%d hover cue requests, row mark on row %d %s (%d pieces, %d dot); Enter: %d select cue requests, cap "
                "%.3f m/s (listed %.3f), list %s"), mSpeedRowHovers, row, mIsSpeedRowItem ? TEXT("yes") : TEXT("NO"),
                mSpeedRowPieces, mSpeedRowDots, selects, cap, listed, panel != nullptr && panel->IsListFocused()
                ? TEXT("STILL FOCUSED") : TEXT("left")));

        // The wheel over the list scrolled the selected row out of view (42f review): no mark, no new hover cue
        Check(TEXT("f-speed-row-scrolled"), mSpeedScrollOffset > 0.0f && mIsSpeedScrolledItem
            && mSpeedScrolledHovers == mSpeedRowHovers && mSpeedScrolledPieces == 0 && mSpeedScrolledDots == 0,
            FString::Printf(TEXT("list scrolled to %.1f, selection and hovered item kept %s; %d hover pieces, %d dots; "
                "%d hover cue requests (want %d)"), mSpeedScrollOffset, mIsSpeedScrolledItem ? TEXT("yes")
                : TEXT("NO"), mSpeedScrolledPieces, mSpeedScrolledDots, mSpeedScrolledHovers, mSpeedRowHovers));
        break;
    }
    case EHoloStep::SpeedClose:
    {
        // Step 6: input back at once, the close drawn part-way, then the widget gone
        const SSOLHoloFrame* speedFrame = GetSpeedFrame(pawn);
        const USOLSpeedPanelWidget* panel = pawn.GetSpeedPanel();
        Check(TEXT("f-speed-esc-input"), mIsInputBackAtEsc, mEscDetail);
        const bool bIsGone = panel != nullptr && !panel->IsInViewport();
        const bool bIsClosed = speedFrame == nullptr || speedFrame->GetAnim().Phase == ESOLHoloMenuPhase::Closed;
        Check(TEXT("f-speed-close"), mIsMidCloseInViewport && mMidClosePhase == ESOLHoloMenuPhase::Closing
            && mMidCloseFraction > 0.0f && mMidCloseFraction < 1.0f && mMidCloseOpacity >= 0.0f
            && mMidCloseOpacity < 1.0f && bIsGone && bIsClosed, FString::Printf(TEXT("mid-close: %s, phase %d, "
                "draw-in %.3f, content opacity %.3f; after %.2f s: widget %s, frame %s"), mIsMidCloseInViewport
                ? TEXT("drawn") : TEXT("NOT drawn"), static_cast<int32>(mMidClosePhase), mMidCloseFraction,
                mMidCloseOpacity, HOLOSMOKE_CLOSED_S, bIsGone ? TEXT("removed") : TEXT("STILL ON THE VIEWPORT"),
                bIsClosed ? TEXT("Closed") : TEXT("NOT closed")));
        break;
    }
    case EHoloStep::SpeedFlat:
    {
        // Step 9, the F3 panel at strength 0 (D18): whole and flat at once, still usable; the cues asked (D21)
        const SSOLHoloFrame* speedFrame = GetSpeedFrame(pawn);
        const int32 opens = holoMenu->GetCueRequestCount(ESOLHoloCue::Open) - mOpenRequestsBefore;
        const int32 hovers = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover) - mHoverRequestsBefore;
        const int32 selects = holoMenu->GetCueRequestCount(ESOLHoloCue::Select) - mSelectRequestsBefore;
        const FSOLHoloFrameOutput* output = speedFrame != nullptr ? &speedFrame->GetLastOutput() : nullptr;
        const float alpha = output != nullptr && output->bHasBacking ? output->Backing.Alpha : -1.0f;
        const int32 fullOutline = speedFrame != nullptr ? FullOutlineCount(*speedFrame) : -1;
        Check(TEXT("f-strength0-open"), mFirstFlatFraction == 1.0f && mMidOpenOpacity == 1.0f && opens == 1,
            FString::Printf(TEXT("first paint after F3: frame fraction %.3f, content opacity %.3f; %d open cue "
                "requests (D21)"), mFirstFlatFraction, mMidOpenOpacity, opens));
        Check(TEXT("f-strength0-flat"), speedFrame != nullptr && speedFrame->GetLastStrength() == 0.0f
            && CountPart(ESOLHoloPart::Sweep, speedFrame) == 0 && alpha == FLAT_PANEL_ALPHA
            && AllLinePads(FRAME_FLAT_PAD_PX, speedFrame)
            && CountPart(ESOLHoloPart::Outline, speedFrame) == fullOutline
            && CountPart(ESOLHoloPart::Hover, speedFrame) == HOVER_MAX_SEGMENTS
            && CountPart(ESOLHoloPart::BrokenDivider, speedFrame) > 0,
            FString::Printf(TEXT("strength %.2f, %d sweeps, backing alpha %.2f, flat pads %s, outline %d of %d pieces, "
                "%d hover pieces, %d divider pieces"), speedFrame != nullptr ? speedFrame->GetLastStrength() : -1.0f,
                CountPart(ESOLHoloPart::Sweep, speedFrame), alpha, AllLinePads(FRAME_FLAT_PAD_PX, speedFrame)
                ? TEXT("yes") : TEXT("NO"), CountPart(ESOLHoloPart::Outline, speedFrame), fullOutline,
                CountPart(ESOLHoloPart::Hover, speedFrame), CountPart(ESOLHoloPart::BrokenDivider, speedFrame)));
        // One hover cue, for Left: the open (Open at once at strength 0) asks for none
        Check(TEXT("f-strength0-usable"), mCapAfterStepMps > 0.0 && mCapAfterStepMps != mCapBeforeMps && selects == 1
            && hovers == 1, FString::Printf(TEXT("Up: cap %.3f -> %.3f m/s; %d select and %d hover cue requests "
                "(D21; want 1 and 1)"), mCapBeforeMps, mCapAfterStepMps, selects, hovers));
        break;
    }
    case EHoloStep::SpeedFlatClose:
        Check(TEXT("f-strength0-close"), mIsFlatClosedAtOnce, FString::Printf(TEXT("Esc at strength 0: widget %s "
            "right after it"), mIsFlatClosedAtOnce ? TEXT("removed") : TEXT("STILL ON THE VIEWPORT")));
        break;
    case EHoloStep::SpeedStrong:
    {
        const SSOLHoloFrame* speedFrame = GetSpeedFrame(pawn);
        const FSOLHoloFrameOutput* output = speedFrame != nullptr ? &speedFrame->GetLastOutput() : nullptr;
        const float alpha = output != nullptr && output->bHasBacking ? output->Backing.Alpha : -1.0f;
        Check(TEXT("f-strength2"), speedFrame != nullptr
            && speedFrame->GetLastStrength() == SOL::Settings::HOLO_MENU_EFFECTS_MAX
            && CountPart(ESOLHoloPart::Sweep, speedFrame) == 1
            && AllLinePads(SOL::Hologram::HUD_GLOW_PAD_PX, speedFrame)
            && FMath::IsNearlyEqual(alpha, 2.0f * BACKDROP_ALPHA_PER_UNIT),
            FString::Printf(TEXT("strength %.2f, %d sweep, glow pads %s, backing alpha %.2f"), speedFrame != nullptr
                ? speedFrame->GetLastStrength() : -1.0f, CountPart(ESOLHoloPart::Sweep, speedFrame),
                AllLinePads(SOL::Hologram::HUD_GLOW_PAD_PX, speedFrame) ? TEXT("yes") : TEXT("NO"), alpha));
        SlateKey(EKeys::Escape, FModifierKeysState());
        SetStrength(SOL::Settings::HOLO_MENU_EFFECTS_DEFAULT);
        pawn.ApplySpeedCapMps(mSpeedCapRestoreMps);
        break;
    }
    case EHoloStep::Quit:
        UE_LOG(LogSOL, Log, TEXT("HoloMenu SUMMARY: %d of %d checks passed (parts 42c-42f; the other menu steps are "
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
// Returns whether a frame's (null: the preview's) last drawn frame equals a fresh BuildFrame of the same inputs
bool FSOLHoloMenuSmoke::IsFrameOutputFresh(const SSOLHoloFrame* frame)
{
    frame = frame != nullptr ? frame : GetPreviewFrame();
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
// Counts a frame's (null: the preview's) last drawn shapes of one part
int32 FSOLHoloMenuSmoke::CountPart(const ESOLHoloPart part, const SSOLHoloFrame* frame) const
{
    frame = frame != nullptr ? frame : GetPreviewFrame();
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
// Returns whether every line and dot (not the fill) of a frame's (null: the preview's) last frame has this glow pad
bool FSOLHoloMenuSmoke::AllLinePads(const float padPx, const SSOLHoloFrame* frame) const
{
    frame = frame != nullptr ? frame : GetPreviewFrame();
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
// Returns the preview's Slate frame, or null
const SSOLHoloFrame* FSOLHoloMenuSmoke::GetPreviewFrame() const
{
    return mFrameWidget.IsValid() && mFrameWidget->GetFrame().IsValid() ? mFrameWidget->GetFrame().Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Returns the Overlay Menu's Slate frame, or null (before the first Ctrl+O)
SSOLHoloFrame* FSOLHoloMenuSmoke::GetOverlayFrame(const ASOLShipPawn& pawn)
{
    const USOLOverlayMenuWidget* menu = pawn.GetOverlayMenu();
    const USOLHoloFrameWidget* frameWidget = menu != nullptr ? menu->GetHoloFrame() : nullptr;
    return frameWidget != nullptr && frameWidget->GetFrame().IsValid() ? frameWidget->GetFrame().Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Returns how many outline pieces a frame draws at its full draw-in for its last pixel layout
int32 FSOLHoloMenuSmoke::FullOutlineCount(const SSOLHoloFrame& frame)
{
    FSOLHoloSegment segments[SOL::HoloMenu::FRAME_MAX_SEGMENTS];
    const FSOLHoloFrameSpec& spec = frame.GetLastPixelSpec();
    return SOLHoloMenu::FrameDrawSegments(spec.Rect, spec.TitleStepWidthPx, spec.UiScale, 1.0f, segments);
}

//////////////////////////////////////////////////////////////////////////
// Sends a key press and release through Slate (to the focused widget), with modifiers
void FSOLHoloMenuSmoke::SlateKey(const FKey& key, const FModifierKeysState& modifiers)
{
    if (!FSlateApplication::IsInitialized())
    {
        return;
    }
    FSlateApplication& slate = FSlateApplication::Get();
    const FKeyEvent keyEvent(key, modifiers, slate.GetUserIndexForKeyboard(), false, 0, 0);
    slate.ProcessKeyDownEvent(keyEvent);
    slate.ProcessKeyUpEvent(keyEvent);
}

//////////////////////////////////////////////////////////////////////////
// Moves the mouse through Slate to a screen point (the OS cursor too, so its next real move lands there)
void FSOLHoloMenuSmoke::SlateMouseMove(const FVector2D& at)
{
    if (!FSlateApplication::IsInitialized())
    {
        return;
    }
    FSlateApplication& slate = FSlateApplication::Get();
    const FVector2D from = slate.GetCursorPos();
    slate.SetCursorPos(at);
    const FPointerEvent move(slate.GetUserIndexForMouse(), FSlateApplicationBase::CursorPointerIndex, at, from,
        TSet<FKey>(), EKeys::Invalid, 0.0f, FModifierKeysState());
    slate.ProcessMouseMoveEvent(move);
    UE_LOG(LogSOL, Log, TEXT("HoloMenu: Slate mouse move to (%.0f, %.0f)"), at.X, at.Y);
}

//////////////////////////////////////////////////////////////////////////
// Moves the mouse to the right of the view, away from the Overlay Menu (so no hover lands on it while it opens)
void FSOLHoloMenuSmoke::ParkMouse(ASOLShipPawn& pawn)
{
    const UGameViewportClient* viewport = pawn.GetWorld()->GetGameViewport();
    const TSharedPtr<SViewport> viewportWidget = viewport != nullptr ? viewport->GetGameViewportWidget() : nullptr;
    if (viewportWidget.IsValid())
    {
        const FGeometry& geometry = viewportWidget->GetCachedGeometry();
        SlateMouseMove(FVector2D(geometry.LocalToAbsolute(geometry.GetLocalSize() * HOLOSMOKE_MOUSE_PARK)));
    }
}

//////////////////////////////////////////////////////////////////////////
// Presses a key with a modifier held through the player controller (game input); both are released next frame
void FSOLHoloMenuSmoke::TapGameChord(const FKey& modifier, const FKey& key)
{
    if (!mController.IsValid())
    {
        return;
    }
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(modifier, IE_Pressed, 1.0f));
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Pressed, 1.0f));
    mPendingReleases.Add(key);
    mPendingReleases.Add(modifier);
}

//////////////////////////////////////////////////////////////////////////
// Runs the select step's presses one every HOLOSMOKE_KEY_GAP_S: Space, Space (the row back), PageDown, PageUp, then
// the profile cycle key (Right Shift + its binding)
void FSOLHoloMenuSmoke::TickOverlaySelect(ASOLShipPawn& pawn, const double elapsedS)
{
    const USOLOverlayMenuWidget* menu = pawn.GetOverlayMenu();
    if (menu == nullptr || mSubStep >= HOLOSMOKE_OVERLAY_SELECT_KEYS
        || elapsedS < static_cast<double>(mSubStep) * HOLOSMOKE_KEY_GAP_S)
    {
        return;
    }
    switch (mSubStep)
    {
    case 0:
        SlateKey(EKeys::SpaceBar, FModifierKeysState());
        break;
    case 1:
        mRowTextToggled = menu->GetRowText(mOverlayRow);
        SlateKey(EKeys::SpaceBar, FModifierKeysState());
        break;
    case 2:
        SlateKey(EKeys::PageDown, FModifierKeysState());
        break;
    case 3:
        mViewedAfterPageDown = menu->GetViewedProfile();
        SlateKey(EKeys::PageUp, FModifierKeysState());
        break;
    default:
    {
        const int32 binding = static_cast<int32>(SOLKeyBindings::EBinding::CycleOverlayProfile);
        const TArray<FString>& keys = mSettings->GetAppliedKeys();
        if (keys.IsValidIndex(binding))
        {
            SlateKey(FKey(*keys[binding]), FModifierKeysState(false, true, false, false, false, false, false, false,
                false));
        }
        break;
    }
    }
    ++mSubStep;
}

//////////////////////////////////////////////////////////////////////////
// Runs a reverse step (the Overlay Menu's, or the F3 panel's when bIsSpeed): once Open, Esc; half-way through the
// close, the open key (the pawn reopens it next frame, which reverses the close); the reversal is read; once Open
// again, Esc; done once closed
bool FSOLHoloMenuSmoke::TickReverse(ASOLShipPawn& pawn, const bool bIsSpeed, const double elapsedS)
{
    const SSOLHoloFrame* overlayFrame = bIsSpeed ? GetSpeedFrame(pawn) : GetOverlayFrame(pawn);
    const UUserWidget* menu = bIsSpeed ? static_cast<const UUserWidget*>(pawn.GetSpeedPanel())
        : static_cast<const UUserWidget*>(pawn.GetOverlayMenu());
    const bool bIsMenuOpen = bIsSpeed ? pawn.IsSpeedPanelOpen() : pawn.IsOverlayMenuOpen();
    const bool bIsOpen = overlayFrame != nullptr && overlayFrame->GetAnim().Phase == ESOLHoloMenuPhase::Open;
    switch (mSubStep)
    {
    case 0:
        if (bIsMenuOpen && bIsOpen && elapsedS >= HOLOSMOKE_OPENED_S)
        {
            SlateKey(EKeys::Escape, FModifierKeysState());
            mSubStepS = elapsedS;
            ++mSubStep;
        }
        break;
    case 1:
        if (elapsedS >= mSubStepS + 0.5 * SOL::HoloMenu::CLOSE_S)
        {
            if (bIsSpeed)
            {
                TapGameKey(GetSpeedPanelKey());
            }
            else
            {
                TapGameChord(EKeys::LeftControl, EKeys::O);
            }
            ++mSubStep;
        }
        break;
    case 2:
        if (bIsMenuOpen && overlayFrame != nullptr && menu != nullptr)
        {
            mReversePhase = overlayFrame->GetAnim().Phase;
            mReverseProgress = overlayFrame->GetAnim().PhaseStartProgress;
            mIsReverseInViewport = menu->IsInViewport();
            mSubStepS = elapsedS;
            ++mSubStep;
        }
        break;
    case 3:
        if (bIsOpen && elapsedS >= mSubStepS + HOLOSMOKE_OPENED_S)
        {
            SlateKey(EKeys::Escape, FModifierKeysState());
            mSubStepS = elapsedS;
            ++mSubStep;
        }
        break;
    case 4:
        if (elapsedS >= mSubStepS + HOLOSMOKE_CLOSED_S)
        {
            ++mSubStep;
        }
        break;
    default:
        return true;
    }
    return elapsedS >= HOLOSMOKE_REVERSE_TIMEOUT_S;
}

//////////////////////////////////////////////////////////////////////////
// Runs the F3 key steps' presses one every HOLOSMOKE_KEY_GAP_S. Digit step: Up (a cap applied), then Left (the
// highlight moves). List step: Tab, a wheel notch down, the row mark read with a screenshot, the wheel over the list
// (the row scrolled out of view) and its mark read, then Enter. Returns true once the step is done
bool FSOLHoloMenuSmoke::TickSpeedKeys(ASOLShipPawn& pawn, const bool bIsList, const double elapsedS)
{
    const USOLSpeedPanelWidget* panel = pawn.GetSpeedPanel();
    if (panel == nullptr || elapsedS < mSubStepS + (mSubStep == 0 ? 0.0 : HOLOSMOKE_KEY_GAP_S))
    {
        return panel == nullptr;
    }
    if (!bIsList)
    {
        // Up, then Left, then the screenshot of the moved mark once its hover glow settled
        if (mSubStep == 0)
        {
            SlateKey(EKeys::Up, FModifierKeysState());
        }
        else if (mSubStep == 1)
        {
            mCapAfterStepMps = pawn.GetComposedControl().SpeedCapMps;
            SlateKey(EKeys::Left, FModifierKeysState());
        }
        else
        {
            return ShotThenDone(TEXT("Speed_Digit"), elapsedS >= mSubStepS + HOLOSMOKE_SETTLED_S);
        }
        ++mSubStep;
        mSubStepS = elapsedS;
        return false;
    }

    // Tab, the wheel, then (once the hover settled) the row mark read with a screenshot, then Enter
    const SSOLHoloFrame* speedFrame = GetSpeedFrame(pawn);
    switch (mSubStep)
    {
    case 0:
        SlateKey(EKeys::Tab, FModifierKeysState());
        break;
    case 1:
        SlateWheel(pawn, -1.0f);
        break;
    case 2:
        if (elapsedS < mSubStepS + HOLOSMOKE_SETTLED_S)
        {
            return false;
        }
        mSpeedRowHovers = mHoloMenu->GetCueRequestCount(ESOLHoloCue::Hover) - mHoverRequestsBefore;
        mSpeedRowPieces = CountPart(ESOLHoloPart::Hover, speedFrame);
        mSpeedRowDots = CountPart(ESOLHoloPart::HoverDot, speedFrame);
        mIsSpeedRowItem = speedFrame != nullptr && panel->IsListFocused()
            && panel->GetListSelection() == mSpeedListBefore + 1
            && speedFrame->GetAnim().HoveredItem == panel->GetHighlightedItem();
        TakeUiShot(TEXT("Speed_Row"));
        break;
    case 3:
        // The wheel over the list itself: its scroll box takes it, the selection stays and scrolls out of view
        SlateWheelAt(panel->GetListScreenCenter(), -HOLOSMOKE_LIST_SCROLL_NOTCHES);
        break;
    case 4:
        if (elapsedS < mSubStepS + HOLOSMOKE_STEP_S)
        {
            return false;
        }
        mSpeedScrollOffset = panel->GetListScrollOffset();
        mSpeedScrolledHovers = mHoloMenu->GetCueRequestCount(ESOLHoloCue::Hover) - mHoverRequestsBefore;
        mSpeedScrolledPieces = CountPart(ESOLHoloPart::Hover, speedFrame);
        mSpeedScrolledDots = CountPart(ESOLHoloPart::HoverDot, speedFrame);
        mIsSpeedScrolledItem = speedFrame != nullptr && panel->IsListFocused()
            && panel->GetListSelection() == mSpeedListBefore + 1
            && speedFrame->GetAnim().HoveredItem == panel->GetHighlightedItem();
        TakeUiShot(TEXT("Speed_RowScrolled"));
        break;
    case 5:
        SlateKey(EKeys::Enter, FModifierKeysState());
        break;
    default:
        return elapsedS >= mSubStepS + HOLOSMOKE_STEP_S;
    }
    ++mSubStep;
    mSubStepS = elapsedS;
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Returns the speed-panel binding's key from the applied settings (F3 by default)
FKey FSOLHoloMenuSmoke::GetSpeedPanelKey() const
{
    const int32 binding = static_cast<int32>(SOLKeyBindings::EBinding::SpeedPanel);
    const TArray<FString>& keys = mSettings->GetAppliedKeys();
    return keys.IsValidIndex(binding) ? FKey(*keys[binding]) : EKeys::F3;
}

//////////////////////////////////////////////////////////////////////////
// Returns the speed panel's Slate frame, or null (before the first F3)
SSOLHoloFrame* FSOLHoloMenuSmoke::GetSpeedFrame(const ASOLShipPawn& pawn)
{
    const USOLSpeedPanelWidget* panel = pawn.GetSpeedPanel();
    const USOLHoloFrameWidget* frameWidget = panel != nullptr ? panel->GetHoloFrame() : nullptr;
    return frameWidget != nullptr && frameWidget->GetFrame().IsValid() ? frameWidget->GetFrame().Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Presses a game key through the player controller (game input); it is released next frame
void FSOLHoloMenuSmoke::TapGameKey(const FKey& key)
{
    if (!mController.IsValid())
    {
        return;
    }
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Pressed, 1.0f));
    mPendingReleases.Add(key);
}

//////////////////////////////////////////////////////////////////////////
// Turns the mouse wheel through Slate at the parked mouse point (over the speed panel's screen dim, away from its
// list, so the panel rather than the list's scroll box takes it)
void FSOLHoloMenuSmoke::SlateWheel(ASOLShipPawn& pawn, const float delta)
{
    const UGameViewportClient* viewport = pawn.GetWorld()->GetGameViewport();
    const TSharedPtr<SViewport> viewportWidget = viewport != nullptr ? viewport->GetGameViewportWidget() : nullptr;
    if (!viewportWidget.IsValid() || !FSlateApplication::IsInitialized())
    {
        return;
    }
    const FGeometry& geometry = viewportWidget->GetCachedGeometry();
    SlateWheelAt(FVector2D(geometry.LocalToAbsolute(geometry.GetLocalSize() * HOLOSMOKE_MOUSE_PARK)), delta);
}

//////////////////////////////////////////////////////////////////////////
// Turns the mouse wheel through Slate at a screen point (the widget under it takes it first)
void FSOLHoloMenuSmoke::SlateWheelAt(const FVector2D& at, const float delta)
{
    if (!FSlateApplication::IsInitialized())
    {
        return;
    }
    FSlateApplication& slate = FSlateApplication::Get();
    const FPointerEvent wheel(slate.GetUserIndexForMouse(), FSlateApplicationBase::CursorPointerIndex, at, at,
        TSet<FKey>(), EKeys::MouseWheelAxis, delta, FModifierKeysState());
    slate.ProcessMouseWheelOrGestureEvent(wheel, nullptr);
    UE_LOG(LogSOL, Log, TEXT("HoloMenu: Slate wheel %.0f at (%.0f, %.0f)"), delta, at.X, at.Y);
}

//////////////////////////////////////////////////////////////////////////
// Section 12 step 11 (D17): a Settings page's embedded Overlay list has no frame widget anywhere in its tree, keeps
// its clear panel and full opacity, and is marked embedded
void FSOLHoloMenuSmoke::CheckEmbeddedOverlay()
{
    USOLSettingsPageWidget* page = mController.IsValid()
        ? CreateWidget<USOLSettingsPageWidget>(mController.Get(), USOLSettingsPageWidget::StaticClass()) : nullptr;
    const USOLOverlayMenuWidget* list = page != nullptr ? page->GetOverlayList() : nullptr;
    int32 frames = 0;
    if (list != nullptr && list->WidgetTree != nullptr)
    {
        list->WidgetTree->ForEachWidget([&frames](UWidget* widget)
        {
            frames += widget != nullptr && widget->IsA<USOLHoloFrameWidget>() ? 1 : 0;
        });
    }
    Check(TEXT("o-embedded-plain"), list != nullptr && list->IsEmbedded() && list->GetHoloFrame() == nullptr
        && frames == 0 && list->GetPanelColor() == FLinearColor::Transparent && list->GetContentOpacity() == 1.0f,
        FString::Printf(TEXT("Settings page list %s, %s, %d frame widgets, panel colour alpha %.2f, opacity %.2f"),
            list != nullptr ? TEXT("found") : TEXT("MISSING"), list != nullptr && list->IsEmbedded()
            ? TEXT("embedded") : TEXT("NOT embedded"), frames, list != nullptr ? list->GetPanelColor().A : -1.0f,
            list != nullptr ? list->GetContentOpacity() : -1.0f));
}

//////////////////////////////////////////////////////////////////////////
// Logs the section 12 checks later parts fill in
void FSOLHoloMenuSmoke::LogStubs()
{
    // SDD 42 section 12: the step, what it will check and the part that adds it
    static const TCHAR* const STUBS[] = {
        TEXT("7 info / target / speed / date-and-anchor cards on the menu layer, 8 ticks each (42g)"),
        TEXT("8 map list menu: open, hover, select cues (42g)"),
        TEXT("9 strength 0 in the map list: instant open, flat look, still usable, cues requested (42g; the "
            "radial's, the Overlay Menu's and the F3 panel's parts run here)"),
        TEXT("10 Settings: slider under the HUD effects slider, Apply writes once (covered by -SOLMenuSmoke; the "
            "cursor part runs here)"),
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
