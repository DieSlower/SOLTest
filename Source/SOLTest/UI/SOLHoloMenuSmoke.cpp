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
#include "Targeting/SOLTargetingSubsystem.h"
#include "UI/SOLActionListWidget.h"
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
        CardsAlwaysOn,  // Step 7: the speed and date-and-anchor cards on the menu layer at the menu strength (8 ticks,
                        // no outline) and no longer under a HUD-layer panel
        CardInfoPrep,   // The info card closed first (Info cleared, its close played out)
        CardInfoOpen,   // Step 7: Info on the radial body: the open cue, the corner ticks mid draw-in, then Open
        CardInfoAgain,  // Info again on the same object: no restart, no cue
        CardTargetOpen, // Step 7: the target cleared, then Target on the radial body: the target card draws in, no cue
        CardTargetCycle,    // R (next target): the target card stays Open and whole and only swaps its text (Q15)
        CardsShot,      // All four cards on the menu layer (screenshot at strength 1)
        CardsClose,     // Step 7: target and Info cleared: both close from their cache, then "TARGET none" on the HUD
        CardsFlat,      // Step 9: strength 0: Info and Target whole on the first frame, flat and readable, closed at
                        // once
        CardsStrong,    // Strength 2: glow pads, the sweep and the darker tint (screenshot); then both cleared
        MapOpen,        // Step 8: the jump map opened (the list menu's steps run on it)
        ListOpen,       // Step 8: the list menu on the radial body: the open cue, the draw-in mid-way, then Open with
                        // the outline, the hint divider and the mark on the highlighted row
        ListHover,      // Step 8: the mouse onto a row, then onto the Target row: two hover cues, brackets and pointer
        ListSelect,     // Step 8: Enter on Target (enabled): the select cue, the action runs, input back to the map in
                        // the same frame, the close plays, the widget gone after CLOSE_S
        ListCancel,     // Up onto the disabled Target row and Enter: nothing runs, no select cue; Esc: no select cue
        ListReopen,     // Esc, then the list opened at another point mid-close: one open cue, on the viewport, focused
        ListFlat,      // Step 9: strength 0: whole and flat at once, Down and Up move the mark (hover cues)
        ListFlatSelect, // Enter on Target at strength 0: the select cue, the action runs, the widget removed at once
        ListStrong,     // Strength 2: the glow look (screenshot), then Esc
        MapClose,       // The map closed mid-close of the list: the list off the viewport and Closed on that frame
        Stubs,         // The section 12 checks later parts fill in
        Quit,           // The SUMMARY line, then quit
        Count,
    };

    constexpr int32 HOLOSMOKE_MIN_FRAMES = 3;
    constexpr int32 HOLOSMOKE_SHOT_FRAMES = 3;                 // Frames a step waits after its screenshot request
    constexpr int32 HOLOSMOKE_HUD_FRAMES = 2;                  // Frames a step waits after its time is up (the HUD
                                                               // drew past it)
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
    constexpr double HOLOSMOKE_MAP_SETTLE_S = 1.0;         // The jump map opened and drawn before the list opens
    constexpr int32 HOLOSMOKE_LIST_HOVER_STEP = 2;         // The first hovered row: this many rows below Target
    const FVector2D HOLOSMOKE_LIST_REOPEN_AT(0.35, 0.4);   // Where the list reopens mid-close (a viewport fraction)

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

    //////////////////////////////////////////////////////////////////////////
    // Returns a HUD card's name for the log
    const TCHAR* HoloSmokeCardName(const ESOLHudCard card)
    {
        switch (card)
        {
        case ESOLHudCard::Info:
            return TEXT("info");
        case ESOLHudCard::Target:
            return TEXT("target");
        case ESOLHudCard::Speed:
            return TEXT("speed");
        case ESOLHudCard::DateAnchor:
            return TEXT("date-and-anchor");
        default:
            return TEXT("?");
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns how many of the HUD's cards the last frame drew
    int32 HoloSmokeCardsDrawn(const ASOLFlightHud& hud)
    {
        int32 cards = 0;
        for (int32 card = 0; card < static_cast<int32>(ESOLHudCard::Count); ++card)
        {
            cards += hud.GetLastCard(static_cast<ESOLHudCard>(card)).bDrawn ? 1 : 0;
        }
        return cards;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a card was drawn frameless on the menu layer: its 8 corner ticks and no outline or other shape
    bool HoloSmokeIsCardFrameless(const ASOLFlightHud::FSOLHudCardStats& stats)
    {
        return stats.bDrawn && stats.bOnMenuLayer && stats.Ticks == SOL::HoloMenu::CARD_TICK_SEGMENTS
            && stats.Others == 0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns what a card drew, for the log
    FString HoloSmokeCardText(const ASOLFlightHud::FSOLHudCardStats& stats)
    {
        return FString::Printf(TEXT("%s on the %s layer, %d shapes (%d ticks, %d sweeps, %d other), tick pad %.2f, "
            "tint %.3f, strength %.2f, draw-in %.2f, content %d shapes %d texts pad %.2f"), stats.bDrawn
            ? TEXT("drawn") : TEXT("NOT DRAWN"), stats.bOnMenuLayer ? TEXT("menu") : TEXT("HUD"), stats.Shapes,
            stats.Ticks, stats.Sweeps, stats.Others, stats.TickPadPx, stats.BackingAlpha, stats.Strength,
            stats.Look.FrameFraction, stats.ContentShapes, stats.ContentTexts, stats.ContentPadPx);
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
    UE_LOG(LogSOL, Log, TEXT("HoloMenu: scripted hologram menu checks starting (parts 42c-42g)"));
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
    mTimeUpFrame = INDEX_NONE;
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
    case EHoloStep::CardsAlwaysOn:
        // A menu strength that differs from the HUD slider's: the always-on cards must follow it (SDD 42 section 4.3)
        SetStrength(HOLOSMOKE_CURSOR_STRENGTH);
        break;
    case EHoloStep::CardInfoPrep:
        SetStrength(SOL::Settings::HOLO_MENU_EFFECTS_DEFAULT);
        mInteraction->ClearInfo();
        break;
    case EHoloStep::CardInfoOpen:
        mOpenRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Open);
        mMidOpenFraction = -1.0f;
        mCardMidTicks = -1;
        mIsMidOpenShot = false;
        mIsCardExecuted = mInteraction->Execute(mRadialBody, ESOLAction::Info);
        break;
    case EHoloStep::CardInfoAgain:
        mOpenRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Open);
        mCardPhaseStartS = mHud.IsValid() ? mHud->GetInfoCardAnim().PhaseStartS : -1.0;
        mIsCardExecuted = mInteraction->Execute(mRadialBody, ESOLAction::Info);
        break;
    case EHoloStep::CardTargetOpen:
        // The target cleared first (its close plays out), then Target in TickStep
        mOpenRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Open);
        mMidOpenFraction = -1.0f;
        mCardMidTicks = -1;
        mIsMidOpenShot = false;
        mIsCardExecuted = false;
        mSubStep = 0;
        mSubStepS = 0.0;
        if (USOLTargetingSubsystem* targeting = pawn.GetWorld()->GetSubsystem<USOLTargetingSubsystem>())
        {
            targeting->ClearTarget();
        }
        break;
    case EHoloStep::CardTargetCycle:
    {
        // R, as the key does: the next farther target replaces the open card's target
        const USOLTargetingSubsystem* targeting = pawn.GetWorld()->GetSubsystem<USOLTargetingSubsystem>();
        const FSOLTargetInfo* before = targeting != nullptr ? targeting->GetSelectedTarget() : nullptr;
        mCycleNameBefore = before != nullptr ? before->Name : NAME_None;
        mCardPhaseStartS = mHud.IsValid() ? mHud->GetTargetCardAnim().PhaseStartS : -1.0;
        mCycleMinFraction = 2.0f;
        mCycleMinFill = 2.0f;
        mIsCycleAlwaysDrawn = true;
        pawn.HandleNextTarget();
        break;
    }
    case EHoloStep::CardsShot:
        mIsCardExecuted = mInteraction->Execute(mRadialBody, ESOLAction::Info);
        break;
    case EHoloStep::CardsClose:
        mMidCloseFraction = -1.0f;
        mMidClosePhase = ESOLHoloMenuPhase::Closed;
        mCardMidTicks = -1;
        mCardTargetMidFraction = -1.0f;
        mCardTargetMidPhase = ESOLHoloMenuPhase::Closed;
        mCardTargetMidTicks = -1;
        mCardTargetRect = FBox2f(ForceInit);
        mIsMidCloseOpen = false;
        if (USOLTargetingSubsystem* targeting = pawn.GetWorld()->GetSubsystem<USOLTargetingSubsystem>())
        {
            targeting->ClearTarget();
        }
        mInteraction->ClearInfo();
        break;
    case EHoloStep::CardsFlat:
        // Strength 0: both opened in one frame, whole on the first drawn frame; cleared later in TickStep
        SetStrength(0.0f);
        mOpenRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Open);
        mFirstFlatFraction = -1.0f;
        mFirstFlatFill = -1.0f;
        mCardTargetMidFraction = -1.0f;
        mIsFlatClosedAtOnce = false;
        mSubStep = 0;
        mIsCardExecuted = mInteraction->Execute(mRadialBody, ESOLAction::Info)
            && mInteraction->Execute(mRadialBody, ESOLAction::Target);
        break;
    case EHoloStep::CardsStrong:
        SetStrength(SOL::Settings::HOLO_MENU_EFFECTS_MAX);
        mIsCardExecuted = mInteraction->Execute(mRadialBody, ESOLAction::Info)
            && mInteraction->Execute(mRadialBody, ESOLAction::Target);
        break;
    case EHoloStep::MapOpen:
        // No target, so Target is an enabled row of the list (Target runs no card cue); the mouse away from the list
        ClearTarget(pawn);
        ParkMouse(pawn);
        pawn.HandleOpenMap(false);
        break;
    case EHoloStep::ListOpen:
        mOpenRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Open);
        mHoverRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover);
        mMidOpenFraction = -1.0f;
        mMidOpenOpacity = -1.0f;
        mIsMidOpenShot = false;
        OpenListOnBody(pawn);
        break;
    case EHoloStep::ListHover:
    {
        // Onto a row below Target now, onto Target later in the step
        const USOLActionListWidget* list = pawn.GetActionList();
        const int32 rows = list != nullptr ? list->GetActions().Num : 0;
        mHoverRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover);
        mIsSecondDrag = false;
        mListHoverRow = rows > 0 && mListTargetRow != INDEX_NONE ? (mListTargetRow + HOLOSMOKE_LIST_HOVER_STEP) % rows
            : INDEX_NONE;
        if (list != nullptr && mListHoverRow != INDEX_NONE)
        {
            SlateMouseMove(list->GetRowScreenCenter(mListHoverRow));
        }
        break;
    }
    case EHoloStep::ListSelect:
        // Enter on the highlighted Target row; the pawn closes the list at its next tick (TickStep reads that frame)
        mSelectRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Select);
        mExecutedBefore = mInteraction->GetExecutedCount();
        mSubStep = 0;
        mSubStepS = 0.0;
        mIsInputBackAtEsc = false;
        mEscDetail = TEXT("the list never closed");
        mIsMidCloseInViewport = false;
        mMidCloseOpacity = -1.0f;
        mMidCloseFraction = -1.0f;
        mMidClosePhase = ESOLHoloMenuPhase::Closed;
        SlateKey(EKeys::Enter, FModifierKeysState());
        break;
    case EHoloStep::ListCancel:
        // Target is now the selected target, so its row is disabled; the presses run in TickStep
        mOpenRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Open);
        mHoverRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover);
        mSelectRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Select);
        mExecutedBefore = mInteraction->GetExecutedCount();
        mSubStep = 0;
        mSubStepS = 0.0;
        mListRowAfterUp = INDEX_NONE;
        mIsListOpenAfterDisabled = false;
        ParkMouse(pawn);
        OpenListOnBody(pawn);
        break;
    case EHoloStep::ListReopen:
        // Opened here, Esc once Open, then the reopen at another point mid-close (TickStep)
        mSubStep = 0;
        mSubStepS = 0.0;
        mIsListClosingAtReopen = false;
        mListReopenOpens = -1;
        mIsListReopened = false;
        mListReopenDetail = TEXT("the reopen never ran");
        ParkMouse(pawn);
        OpenListOnBody(pawn);
        break;
    case EHoloStep::ListFlat:
    {
        // Strength 0: opened, then (TickStep) Down and Up
        SetStrength(0.0f);
        const SSOLHoloFrame* listFrame = GetListFrame(pawn);
        mOpenRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Open);
        mHoverRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover);
        mPaintsBefore = listFrame != nullptr ? listFrame->GetPaintCount() : 0;
        mFirstFlatFraction = -1.0f;
        mMidOpenOpacity = -1.0f;
        mSubStep = 0;
        mSubStepS = 0.0;
        ParkMouse(pawn);
        OpenListOnBody(pawn);
        break;
    }
    case EHoloStep::ListFlatSelect:
        mSelectRequestsBefore = holoMenu->GetCueRequestCount(ESOLHoloCue::Select);
        mExecutedBefore = mInteraction->GetExecutedCount();
        mIsFlatClosedAtOnce = false;
        mSubStep = 0;
        SlateKey(EKeys::Enter, FModifierKeysState());
        break;
    case EHoloStep::ListStrong:
        ClearTarget(pawn);
        SetStrength(SOL::Settings::HOLO_MENU_EFFECTS_MAX);
        mOverlayOpenS = -1.0;
        ParkMouse(pawn);
        OpenListOnBody(pawn);
        break;
    case EHoloStep::MapClose:
    {
        // The map closes (TickStep) while the list plays the close ListStrong's Esc began: the map's close must remove
        // it at once (Dismiss), which the first frame with the map closed tells apart from the close's natural end
        mIsListClosingAtMapClose = false;
        mIsListGoneAtMapClose = false;
        mMapCloseDetail = TEXT("the map never closed");
        mSubStep = 0;
        mSubStepS = 0.0;
        TickMapClose(pawn, 0.0);
        break;
    }
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
    case EHoloStep::CardsAlwaysOn:
        return ShotThenDone(TEXT("Cards_Always"), elapsedS >= HOLOSMOKE_SETTLED_S);
    case EHoloStep::CardInfoPrep:
        return HudDrawnThenDone(elapsedS >= HOLOSMOKE_CLOSED_S);
    case EHoloStep::CardInfoOpen:
        // The corner ticks caught about half-way through the draw-in (the last HUD frame), with a screenshot
        if (hud != nullptr && mMidOpenFraction < 0.0f && elapsedS >= HOLOSMOKE_MID_OPEN_S)
        {
            const ASOLFlightHud::FSOLHudCardStats& info = hud->GetLastCard(ESOLHudCard::Info);
            mMidOpenFraction = info.bDrawn ? info.Look.FrameFraction : 0.0f;
            mCardMidTicks = info.Ticks;
            TakeUiShot(TEXT("InfoCardMidOpen"));
            mIsMidOpenShot = true;
        }
        return HudDrawnThenDone(elapsedS >= HOLOSMOKE_OPENED_S);
    case EHoloStep::CardTargetOpen:
    {
        // Target once the cleared target's close played out, then the draw-in caught mid-way
        if (mSubStep == 0 && elapsedS >= HOLOSMOKE_CLOSED_S)
        {
            mOpenRequestsBefore = mHoloMenu->GetCueRequestCount(ESOLHoloCue::Open);
            mIsCardExecuted = mInteraction->Execute(mRadialBody, ESOLAction::Target);
            mSubStep = 1;
            mSubStepS = elapsedS;
        }
        if (mSubStep == 1 && hud != nullptr && mMidOpenFraction < 0.0f && elapsedS >= mSubStepS
            + HOLOSMOKE_MID_OPEN_S)
        {
            const ASOLFlightHud::FSOLHudCardStats& target = hud->GetLastCard(ESOLHudCard::Target);
            mMidOpenFraction = target.bDrawn ? target.Look.FrameFraction : 0.0f;
            mCardMidTicks = target.Ticks;
            TakeUiShot(TEXT("TargetCardMidOpen"));
            mIsMidOpenShot = true;
        }
        return mSubStep == 1 && HudDrawnThenDone(elapsedS >= mSubStepS + HOLOSMOKE_OPENED_S);
    }
    case EHoloStep::CardTargetCycle:
    {
        // Every HUD frame after R: the card drawn, whole and filled (no draw-in replay), for longer than an open
        if (hud != nullptr)
        {
            const ASOLFlightHud::FSOLHudCardStats& target = hud->GetLastCard(ESOLHudCard::Target);
            mIsCycleAlwaysDrawn = mIsCycleAlwaysDrawn && target.bDrawn;
            mCycleMinFraction = FMath::Min(mCycleMinFraction, target.bDrawn ? target.Look.FrameFraction : 0.0f);
            mCycleMinFill = FMath::Min(mCycleMinFill, target.bDrawn ? target.Look.FillAlpha : 0.0f);
        }
        return HudDrawnThenDone(elapsedS >= HOLOSMOKE_OPENED_S);
    }
    case EHoloStep::CardsShot:
        return ShotThenDone(TEXT("Cards_S1"), elapsedS >= HOLOSMOKE_SETTLED_S);
    case EHoloStep::CardsClose:
        // Both cards caught half-way through their close, drawn from the cache after the target and Info went away
        if (hud != nullptr && mMidCloseFraction < 0.0f && elapsedS >= HOLOSMOKE_MID_CLOSE_S)
        {
            const ASOLFlightHud::FSOLHudCardStats& info = hud->GetLastCard(ESOLHudCard::Info);
            const ASOLFlightHud::FSOLHudCardStats& target = hud->GetLastCard(ESOLHudCard::Target);
            const USOLTargetingSubsystem* targeting = pawn.GetWorld()->GetSubsystem<USOLTargetingSubsystem>();
            mMidCloseFraction = info.bDrawn ? info.Look.FrameFraction : 0.0f;
            mMidClosePhase = hud->GetInfoCardAnim().Phase;
            mCardMidTicks = info.Others == 0 ? info.Ticks : -1;
            mCardTargetMidFraction = target.bDrawn ? target.Look.FrameFraction : 0.0f;
            mCardTargetMidPhase = hud->GetTargetCardAnim().Phase;
            mCardTargetMidTicks = target.Others == 0 ? target.Ticks : -1;
            mCardTargetRect = target.Rect;
            mIsMidCloseOpen = mInteraction->GetInfoObject().IsValid()
                || (targeting != nullptr && targeting->GetSelectedTarget() != nullptr);
            TakeUiShot(TEXT("CardsClosing"));
        }
        return HudDrawnThenDone(elapsedS >= HOLOSMOKE_CLOSED_S);
    case EHoloStep::CardsFlat:
    {
        // The first drawn frame after the opens; the screenshot; then both cleared and, a frame later, gone at once
        if (hud != nullptr && mFirstFlatFraction < 0.0f)
        {
            const ASOLFlightHud::FSOLHudCardStats& info = hud->GetLastCard(ESOLHudCard::Info);
            const ASOLFlightHud::FSOLHudCardStats& target = hud->GetLastCard(ESOLHudCard::Target);
            if (info.bDrawn || target.bDrawn)
            {
                mFirstFlatFraction = info.bDrawn ? info.Look.FrameFraction : 0.0f;
                mFirstFlatFill = info.bDrawn ? info.Look.FillAlpha : 0.0f;
                mCardTargetMidFraction = target.bDrawn ? target.Look.FrameFraction : 0.0f;
            }
        }
        if (mSubStep == 0 && ShotThenDone(TEXT("Cards_S0"), elapsedS >= HOLOSMOKE_SETTLED_S))
        {
            // The checks of the open look read this frame's cards in ExitStep, so they are copied before the clear
            if (hud != nullptr)
            {
                mFlatCardStats[0] = hud->GetLastCard(ESOLHudCard::Info);
                mFlatCardStats[1] = hud->GetLastCard(ESOLHudCard::Target);
                mFlatCardStats[2] = hud->GetLastCard(ESOLHudCard::Speed);
                mFlatCardStats[3] = hud->GetLastCard(ESOLHudCard::DateAnchor);
                mFlatInfoLines = hud->GetLastInfoCardLineCount();
                mFlatMenuTexts = hud->GetLastMenuPaintedTextCount();
            }
            if (USOLTargetingSubsystem* targeting = pawn.GetWorld()->GetSubsystem<USOLTargetingSubsystem>())
            {
                targeting->ClearTarget();
            }
            mInteraction->ClearInfo();
            mSubStep = 1;
            return false;
        }
        if (mSubStep == 1)
        {
            // The frame after the clear: at strength 0 the close is instant (nothing drawn of either card)
            mIsFlatClosedAtOnce = hud != nullptr && !hud->GetLastCard(ESOLHudCard::Info).bDrawn
                && !hud->GetLastCard(ESOLHudCard::Target).bDrawn
                && hud->GetInfoCardAnim().Phase == ESOLHoloMenuPhase::Closed
                && hud->GetTargetCardAnim().Phase == ESOLHoloMenuPhase::Closed;
            mSubStep = 2;
        }
        return mSubStep == 2;
    }
    case EHoloStep::CardsStrong:
        return ShotThenDone(TEXT("Cards_S2"), elapsedS >= HOLOSMOKE_SETTLED_S);
    case EHoloStep::MapOpen:
        return elapsedS >= HOLOSMOKE_MAP_SETTLE_S;
    case EHoloStep::ListOpen:
    {
        // The draw-in caught about half-way (with a screenshot), then Open (a screenshot of the opened list)
        const SSOLHoloFrame* listFrame = GetListFrame(pawn);
        const USOLActionListWidget* list = pawn.GetActionList();
        if (listFrame != nullptr && list != nullptr && mMidOpenFraction < 0.0f && elapsedS >= HOLOSMOKE_MID_OPEN_S)
        {
            mMidOpenFraction = listFrame->GetLastLook().FrameFraction;
            mMidOpenOpacity = list->GetContentOpacity();
            TakeUiShot(TEXT("ListMidOpen"));
            mIsMidOpenShot = true;
        }
        return ShotThenDone(TEXT("List_Open"), elapsedS >= HOLOSMOKE_OPENED_S);
    }
    case EHoloStep::ListHover:
        if (!mIsSecondDrag && elapsedS >= HOLOSMOKE_KEY_GAP_S)
        {
            if (const USOLActionListWidget* list = pawn.GetActionList())
            {
                SlateMouseMove(list->GetRowScreenCenter(mListTargetRow));
            }
            mIsSecondDrag = true;
        }
        return ShotThenDone(TEXT("List_Hover"), mIsSecondDrag && elapsedS >= HOLOSMOKE_KEY_GAP_S
            + HOLOSMOKE_SETTLED_S);
    case EHoloStep::ListSelect:
    {
        // The frame the pawn closed the list: input back at once; then the close caught part-way (a screenshot)
        const USOLActionListWidget* list = pawn.GetActionList();
        const SSOLHoloFrame* listFrame = GetListFrame(pawn);
        if (mSubStep == 0 && !pawn.IsActionListOpen())
        {
            const bool bIsClosing = listFrame != nullptr && listFrame->GetAnim().Phase == ESOLHoloMenuPhase::Closing;
            const bool bIsUnfocused = list != nullptr && !list->IsFocusable() && !list->HasAnyUserFocus();
            const bool bIsHitInvisible = list != nullptr
                && list->GetVisibility() == ESlateVisibility::HitTestInvisible;
            mIsInputBackAtEsc = pawn.IsMapOpen() && list != nullptr && list->IsClosing() && list->IsInViewport()
                && bIsClosing && bIsUnfocused && bIsHitInvisible;
            mEscDetail = FString::Printf(TEXT("the frame the list closed: map %s, widget %s and %s, frame %s, %s, %s"),
                pawn.IsMapOpen() ? TEXT("open") : TEXT("CLOSED"), list != nullptr && list->IsClosing()
                ? TEXT("closing") : TEXT("NOT closing"), list != nullptr && list->IsInViewport() ? TEXT("still drawn")
                : TEXT("REMOVED"), bIsClosing ? TEXT("Closing") : TEXT("NOT closing"), bIsUnfocused
                ? TEXT("unfocusable, no keyboard focus") : TEXT("STILL FOCUSABLE OR FOCUSED"), bIsHitInvisible
                ? TEXT("hit-test invisible") : TEXT("STILL HIT-TESTABLE"));
            mSubStep = 1;
            mSubStepS = elapsedS;
        }
        if (mSubStep == 1 && listFrame != nullptr && list != nullptr && elapsedS >= mSubStepS + HOLOSMOKE_MID_CLOSE_S)
        {
            mMidCloseFraction = listFrame->GetLastLook().FrameFraction;
            mMidClosePhase = listFrame->GetAnim().Phase;
            mMidCloseOpacity = list->GetContentOpacity();
            mIsMidCloseInViewport = list->IsInViewport();
            TakeUiShot(TEXT("ListClosing"));
            mSubStep = 2;
        }
        return (mSubStep >= 1 && elapsedS >= mSubStepS + HOLOSMOKE_CLOSED_S) || elapsedS >= HOLOSMOKE_OVERLAY_TIMEOUT_S;
    }
    case EHoloStep::ListCancel:
    {
        // Once Open: Up onto the disabled Target row, Enter (nothing runs), then Esc; one press every key gap
        const USOLActionListWidget* list = pawn.GetActionList();
        if (list == nullptr || mSubStep >= 3)
        {
            return list == nullptr || elapsedS >= mSubStepS + HOLOSMOKE_CLOSED_S;
        }
        const double atS = mSubStep == 0 ? HOLOSMOKE_OPENED_S : mSubStepS + HOLOSMOKE_KEY_GAP_S;
        if (elapsedS >= atS)
        {
            if (mSubStep == 0)
            {
                SlateKey(EKeys::Up, FModifierKeysState());
                mListRowAfterUp = list->GetState().Highlighted;
            }
            else if (mSubStep == 1)
            {
                SlateKey(EKeys::Enter, FModifierKeysState());
            }
            else
            {
                mIsListOpenAfterDisabled = pawn.IsActionListOpen() && list->IsInViewport() && !list->IsClosing();
                SlateKey(EKeys::Escape, FModifierKeysState());
            }
            ++mSubStep;
            mSubStepS = elapsedS;
        }
        return false;
    }
    case EHoloStep::ListReopen:
        return TickListReopen(pawn, elapsedS);
    case EHoloStep::ListFlat:
    {
        // The first paint after the open, then Down and Up (the mark moves, a hover cue each)
        const SSOLHoloFrame* listFrame = GetListFrame(pawn);
        const USOLActionListWidget* list = pawn.GetActionList();
        if (listFrame != nullptr && list != nullptr && pawn.IsActionListOpen() && mFirstFlatFraction < 0.0f
            && listFrame->GetPaintCount() != mPaintsBefore)
        {
            mFirstFlatFraction = listFrame->GetLastLook().FrameFraction;
            mMidOpenOpacity = list->GetContentOpacity();
            mSubStepS = elapsedS;
        }
        if (list == nullptr || mFirstFlatFraction < 0.0f)
        {
            return elapsedS >= HOLOSMOKE_OVERLAY_TIMEOUT_S;
        }
        if (mSubStep < 2 && elapsedS >= mSubStepS + HOLOSMOKE_KEY_GAP_S)
        {
            SlateKey(mSubStep == 0 ? EKeys::Down : EKeys::Up, FModifierKeysState());
            ++mSubStep;
            mSubStepS = elapsedS;
        }
        return mSubStep >= 2 && ShotThenDone(TEXT("List_S0"), elapsedS >= mSubStepS + HOLOSMOKE_SETTLED_S);
    }
    case EHoloStep::ListFlatSelect:
    {
        // The frame the pawn closed the list: at strength 0 the widget is already off the viewport
        const USOLActionListWidget* list = pawn.GetActionList();
        if (mSubStep == 0 && !pawn.IsActionListOpen())
        {
            mIsFlatClosedAtOnce = list != nullptr && !list->IsInViewport();
            mSubStep = 1;
        }
        return mSubStep == 1 || elapsedS >= HOLOSMOKE_OVERLAY_TIMEOUT_S;
    }
    case EHoloStep::ListStrong:
        if (mOverlayOpenS < 0.0 && pawn.IsActionListOpen())
        {
            mOverlayOpenS = elapsedS;
        }
        return ShotThenDone(TEXT("List_S2"), (mOverlayOpenS >= 0.0 && elapsedS >= mOverlayOpenS
            + HOLOSMOKE_SETTLED_S) || elapsedS >= HOLOSMOKE_OVERLAY_TIMEOUT_S);
    case EHoloStep::MapClose:
        return TickMapClose(pawn, elapsedS);
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

        // The menu layer: there, with the menu material, stamped and holding only the cards with no menu open (the
        // always-on speed and date-and-anchor cards, SDD 42 D15: one backing per card drawn, no other)
        const uint64 stamp = hud != nullptr ? hud->GetMenuLayerBatchFrame() : 0;
        const int32 cards = hud != nullptr ? HoloSmokeCardsDrawn(*hud) : 0;
        const bool bIsCardsOnly = hud != nullptr && cards >= 2 && hud->GetLastMenuBackingCount() == cards
            && hud->GetLastCard(ESOLHudCard::Speed).bOnMenuLayer
            && hud->GetLastCard(ESOLHudCard::DateAnchor).bOnMenuLayer
            && hud->GetLastMenuPaintedShapeCount() > 0;
        Check(TEXT("a-menu-layer"), hud != nullptr && hud->HasMenuLayer() && hud->GetMenuLayerMaterial() == menuMaterial
            && bIsCardsOnly && stamp != 0, FString::Printf(TEXT("menu layer %s, material %s, %d shapes, %d backings "
                "for %d cards, %d painted, stamped at frame %llu (now %llu)"), hud != nullptr && hud->HasMenuLayer()
                ? TEXT("present") : TEXT("MISSING"), hud != nullptr && hud->GetMenuLayerMaterial() == menuMaterial
                ? TEXT("menu MID") : TEXT("OTHER"), hud != nullptr ? hud->GetLastMenuShapeCount() : -1,
                hud != nullptr ? hud->GetLastMenuBackingCount() : -1, cards,
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
        Check(TEXT("k-menu-layer-idle"), hud != nullptr && hud->GetLastMenuBackingCount() == HoloSmokeCardsDrawn(*hud)
            && hud->GetLastRadialSectorCount() == 0, FString::Printf(TEXT("menu layer after the frame preview: %d "
                "shapes, %d backings for %d cards, no radial (the UMG frame never draws on it)"), hud != nullptr
                ? hud->GetLastMenuShapeCount() : -1, hud != nullptr ? hud->GetLastMenuBackingCount() : -1,
                hud != nullptr ? HoloSmokeCardsDrawn(*hud) : -1));
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
        // RADIAL_DISABLED_HOVER_GLOW_SCALE (0, SDD 42 Q13)
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
    case EHoloStep::CardsAlwaysOn:
    {
        // Step 7: the speed and date-and-anchor cards: frameless on the menu layer, at the menu strength (not the HUD
        // slider's), Open with no draw-in, no HUD-layer panel left under either, and their content (text; the speed
        // card's bars and steer icon too) added on the menu layer above the tint, its lines at the menu line pad
        const float strength = HOLOSMOKE_CURSOR_STRENGTH;
        const float tint = SOLHoloMenu::BackdropAlpha(strength, 1.0f);
        const float contentPad = SOLHoloFrameBuild::LinePad(strength);
        const ESOLHudCard cards[] = { ESOLHudCard::Speed,
            ESOLHudCard::DateAnchor };
        int32 passed = 0;
        FString detail;
        for (const ESOLHudCard card : cards)
        {
            const ASOLFlightHud::FSOLHudCardStats stats = hud != nullptr ? hud->GetLastCard(card)
                : ASOLFlightHud::FSOLHudCardStats();
            const int32 hudPanels = hud != nullptr ? hud->CountHudBackingsOverlapping(stats.Rect) : -1;
            const bool bHasContent = stats.ContentTexts > 0 && stats.ContentPadPx == contentPad
                && (card != ESOLHudCard::Speed || stats.ContentShapes > 0);
            passed += HoloSmokeIsCardFrameless(stats) && stats.Sweeps == 1 && stats.Strength == strength
                && FMath::IsNearlyEqual(stats.BackingAlpha, tint) && stats.Look.FrameFraction == 1.0f
                && hudPanels == 0 && bHasContent ? 1 : 0;
            detail += FString::Printf(TEXT("%s: %s, %d HUD-layer backings under it; "), HoloSmokeCardName(card),
                *HoloSmokeCardText(stats), hudPanels);
        }
        Check(TEXT("c-cards-always-on"), passed == 2, FString::Printf(TEXT("menu strength %.2f (HUD effects %.2f, "
            "tint %.3f): %s"), strength, mSettings->GetLiveValues().HudEffectsStrength, tint, *detail));
        break;
    }
    case EHoloStep::CardInfoPrep:
    {
        const bool bIsClosed = hud != nullptr && hud->GetInfoCardAnim().Phase == ESOLHoloMenuPhase::Closed
            && !hud->GetLastCard(ESOLHudCard::Info).bDrawn && hud->GetLastInfoCardLineCount() == 0;
        Check(TEXT("c-info-prep"), bIsClosed && !mInteraction->GetInfoObject().IsValid(), FString::Printf(TEXT("Info "
            "cleared: card %s"), bIsClosed ? TEXT("closed, nothing drawn") : TEXT("STILL DRAWN")));
        break;
    }
    case EHoloStep::CardInfoOpen:
    {
        // The open cue at Info, the ticks caught mid draw-in, then Open, frameless, with its lines on the menu layer
        const int32 opens = holoMenu->GetCueRequestCount(ESOLHoloCue::Open) - mOpenRequestsBefore;
        const ASOLFlightHud::FSOLHudCardStats stats = hud != nullptr
            ? hud->GetLastCard(ESOLHudCard::Info) : ASOLFlightHud::FSOLHudCardStats();
        const bool bIsOpen = hud != nullptr && hud->GetInfoCardAnim().Phase == ESOLHoloMenuPhase::Open;
        const int32 lines = hud != nullptr ? hud->GetLastInfoCardLineCount() : 0;
        Check(TEXT("c-info-open"), mIsCardExecuted && opens == 1 && bIsOpen && HoloSmokeIsCardFrameless(stats)
            && stats.Look.FillAlpha == 1.0f && lines >= 3, FString::Printf(TEXT("Info on body %d %s: %d open cue "
                "requests, phase %s, %s, %d lines"), mRadialBody.Index, mIsCardExecuted ? TEXT("ran")
                : TEXT("DID NOT RUN"), opens, bIsOpen ? TEXT("Open") : TEXT("NOT open"), *HoloSmokeCardText(stats),
                lines));
        Check(TEXT("c-info-mid-open"), mIsMidOpenShot && mMidOpenFraction > 0.0f && mMidOpenFraction < 1.0f
            && mCardMidTicks == SOL::HoloMenu::CARD_TICK_SEGMENTS, FString::Printf(TEXT("tick draw-in %.3f at %.2f "
                "s, %d ticks"), mMidOpenFraction, HOLOSMOKE_MID_OPEN_S, mCardMidTicks));
        break;
    }
    case EHoloStep::CardInfoAgain:
    {
        const int32 opens = holoMenu->GetCueRequestCount(ESOLHoloCue::Open) - mOpenRequestsBefore;
        const double phaseStartS = hud != nullptr ? hud->GetInfoCardAnim().PhaseStartS : -2.0;
        const bool bIsOpen = hud != nullptr && hud->GetInfoCardAnim().Phase == ESOLHoloMenuPhase::Open
            && hud->GetLastCard(ESOLHudCard::Info).Look.FrameFraction == 1.0f;
        Check(TEXT("c-info-again"), mIsCardExecuted && opens == 0 && bIsOpen && phaseStartS == mCardPhaseStartS,
            FString::Printf(TEXT("Info again on the same object: %d open cue requests, %s, phase start %s"), opens,
                bIsOpen ? TEXT("still Open and whole") : TEXT("NOT open"), phaseStartS == mCardPhaseStartS
                ? TEXT("unchanged") : TEXT("RESTARTED")));
        break;
    }
    case EHoloStep::CardTargetOpen:
    {
        // Target: the target card draws in (no cue), frameless; the corner ticks caught mid draw-in
        const int32 opens = holoMenu->GetCueRequestCount(ESOLHoloCue::Open) - mOpenRequestsBefore;
        const ASOLFlightHud::FSOLHudCardStats stats = hud != nullptr
            ? hud->GetLastCard(ESOLHudCard::Target) : ASOLFlightHud::FSOLHudCardStats();
        const bool bIsOpen = hud != nullptr && hud->GetTargetCardAnim().Phase == ESOLHoloMenuPhase::Open;
        Check(TEXT("c-target-open"), mIsCardExecuted && opens == 0 && bIsOpen && HoloSmokeIsCardFrameless(stats)
            && stats.Look.FillAlpha == 1.0f, FString::Printf(TEXT("Target on body %d %s: %d open cue requests, phase "
                "%s, %s"), mRadialBody.Index, mIsCardExecuted ? TEXT("ran") : TEXT("DID NOT RUN"), opens, bIsOpen
                ? TEXT("Open") : TEXT("NOT open"), *HoloSmokeCardText(stats)));
        Check(TEXT("c-target-mid-open"), mIsMidOpenShot && mMidOpenFraction > 0.0f && mMidOpenFraction < 1.0f
            && mCardMidTicks == SOL::HoloMenu::CARD_TICK_SEGMENTS, FString::Printf(TEXT("tick draw-in %.3f at %.2f "
                "s, %d ticks"), mMidOpenFraction, HOLOSMOKE_MID_OPEN_S, mCardMidTicks));
        break;
    }
    case EHoloStep::CardTargetCycle:
    {
        // Q15: another target keeps the card open (same phase start, whole and filled every frame) with the new name
        const USOLTargetingSubsystem* targeting = pawn.GetWorld()->GetSubsystem<USOLTargetingSubsystem>();
        const FSOLTargetInfo* after = targeting != nullptr ? targeting->GetSelectedTarget() : nullptr;
        const FName nameAfter = after != nullptr ? after->Name : NAME_None;
        const bool bIsOpen = hud != nullptr && hud->GetTargetCardAnim().Phase == ESOLHoloMenuPhase::Open
            && hud->GetTargetCardAnim().PhaseStartS == mCardPhaseStartS;
        const FName shown = hud != nullptr ? hud->GetTargetCardName() : NAME_None;
        Check(TEXT("c-target-cycle"), mCycleNameBefore != NAME_None && nameAfter != NAME_None
            && nameAfter != mCycleNameBefore && shown == nameAfter && bIsOpen && mIsCycleAlwaysDrawn
            && mCycleMinFraction == 1.0f && mCycleMinFill == 1.0f, FString::Printf(TEXT("R: target %s -> %s, card "
                "shows %s, %s, drawn every frame %s, smallest draw-in %.2f and fill %.2f"),
                *mCycleNameBefore.ToString(), *nameAfter.ToString(), *shown.ToString(), bIsOpen
                ? TEXT("still Open, phase start unchanged") : TEXT("RESTARTED or not open"), mIsCycleAlwaysDrawn
                ? TEXT("yes") : TEXT("NO"), mCycleMinFraction, mCycleMinFill));
        break;
    }
    case EHoloStep::CardsShot:
    {
        // All four cards at once: each frameless on the menu layer, one backing each and no other menu backing
        int32 frameless = 0;
        FString detail;
        for (int32 card = 0; card < static_cast<int32>(ESOLHudCard::Count); ++card)
        {
            const ESOLHudCard id = static_cast<ESOLHudCard>(card);
            const ASOLFlightHud::FSOLHudCardStats stats = hud != nullptr ? hud->GetLastCard(id)
                : ASOLFlightHud::FSOLHudCardStats();
            frameless += HoloSmokeIsCardFrameless(stats) ? 1 : 0;
            detail += FString::Printf(TEXT("%s: %s; "), HoloSmokeCardName(id), *HoloSmokeCardText(stats));
        }
        const int32 backings = hud != nullptr ? hud->GetLastMenuBackingCount() : -1;
        Check(TEXT("c-cards-four"), frameless == static_cast<int32>(ESOLHudCard::Count)
            && backings == frameless, FString::Printf(TEXT("%d frameless cards, %d menu layer backings: %s"),
                frameless, backings, *detail));
        break;
    }
    case EHoloStep::CardsClose:
    {
        // Both closed from their cache (frameless, mid-way) after the target and Info went away; then nothing, and
        // "TARGET none" back on the HUD layer with its own panel where the target card was
        const bool bIsInfoClosed = hud != nullptr && hud->GetInfoCardAnim().Phase == ESOLHoloMenuPhase::Closed
            && !hud->GetLastCard(ESOLHudCard::Info).bDrawn && hud->GetLastInfoCardLineCount() == 0;
        const bool bIsTargetClosed = hud != nullptr && hud->GetTargetCardAnim().Phase == ESOLHoloMenuPhase::Closed
            && !hud->GetLastCard(ESOLHudCard::Target).bDrawn;
        const int32 nonePanels = hud != nullptr ? hud->CountHudBackingsOverlapping(mCardTargetRect) : -1;
        constexpr int32 ticks = SOL::HoloMenu::CARD_TICK_SEGMENTS;
        Check(TEXT("c-cards-close"), !mIsMidCloseOpen && mMidClosePhase == ESOLHoloMenuPhase::Closing
            && mMidCloseFraction > 0.0f && mMidCloseFraction < 1.0f && mCardMidTicks == ticks
            && mCardTargetMidPhase == ESOLHoloMenuPhase::Closing && mCardTargetMidFraction > 0.0f
            && mCardTargetMidFraction < 1.0f && mCardTargetMidTicks == ticks && bIsInfoClosed && bIsTargetClosed
            && nonePanels > 0, FString::Printf(TEXT("mid-close (Info and target %s): info phase %d, draw-in %.3f, %d "
                "ticks; target phase %d, draw-in %.3f, %d ticks; after %.2f s and %d frames: info %s, target %s, %d HUD "
                "panel(s) for TARGET none"), mIsMidCloseOpen ? TEXT("STILL SET") : TEXT("cleared"),
                static_cast<int32>(mMidClosePhase), mMidCloseFraction, mCardMidTicks,
                static_cast<int32>(mCardTargetMidPhase), mCardTargetMidFraction, mCardTargetMidTicks,
                HOLOSMOKE_CLOSED_S, HOLOSMOKE_HUD_FRAMES, bIsInfoClosed ? TEXT("closed") : TEXT("STILL DRAWN"),
                bIsTargetClosed ? TEXT("closed") : TEXT("STILL DRAWN"), nonePanels));
        break;
    }
    case EHoloStep::CardsFlat:
    {
        // Step 9, the cards at strength 0 (D18): whole at once, flat thin ticks, no sweep, the flat tint, readable;
        // the info card's open cue still asked (D21); closed at once
        const int32 opens = holoMenu->GetCueRequestCount(ESOLHoloCue::Open) - mOpenRequestsBefore;
        Check(TEXT("c-strength0-open"), mIsCardExecuted && mFirstFlatFraction == 1.0f && mFirstFlatFill == 1.0f
            && mCardTargetMidFraction == 1.0f && opens == 1, FString::Printf(TEXT("first drawn frame: info draw-in "
                "%.2f, fill %.2f, target draw-in %.2f; %d open cue requests (D21)"), mFirstFlatFraction,
                mFirstFlatFill, mCardTargetMidFraction, opens));
        int32 flat = 0;
        FString detail;
        for (int32 card = 0; card < static_cast<int32>(ESOLHudCard::Count); ++card)
        {
            // The always-on cards' content lines (bars, steer icon, G meter) go flat with the ticks (D18)
            const ASOLFlightHud::FSOLHudCardStats& stats = mFlatCardStats[card];
            const bool bIsAlwaysOn = card == static_cast<int32>(ESOLHudCard::Speed)
                || card == static_cast<int32>(ESOLHudCard::DateAnchor);
            flat += HoloSmokeIsCardFrameless(stats) && stats.Strength == 0.0f && stats.Sweeps == 0
                && stats.BackingAlpha == FLAT_PANEL_ALPHA && stats.TickPadPx == FRAME_FLAT_PAD_PX
                && (!bIsAlwaysOn || stats.ContentPadPx == FRAME_FLAT_PAD_PX) ? 1 : 0;
            detail += FString::Printf(TEXT("%s: %s; "), HoloSmokeCardName(static_cast<ESOLHudCard>(
                card)), *HoloSmokeCardText(stats));
        }
        Check(TEXT("c-strength0-flat"), flat == static_cast<int32>(ESOLHudCard::Count)
            && mFlatInfoLines >= 3 && mFlatMenuTexts > 0 && mIsFlatClosedAtOnce, FString::Printf(TEXT("%d flat "
                "cards, %d info lines, %d menu layer texts painted, cleared: %s; %s"), flat, mFlatInfoLines,
                mFlatMenuTexts, mIsFlatClosedAtOnce ? TEXT("both gone at once") : TEXT("STILL DRAWN"), *detail));
        break;
    }
    case EHoloStep::CardsStrong:
    {
        int32 strong = 0;
        FString detail;
        for (int32 card = 0; card < static_cast<int32>(ESOLHudCard::Count); ++card)
        {
            const ESOLHudCard id = static_cast<ESOLHudCard>(card);
            const ASOLFlightHud::FSOLHudCardStats stats = hud != nullptr ? hud->GetLastCard(id)
                : ASOLFlightHud::FSOLHudCardStats();
            const bool bIsAlwaysOn = id == ESOLHudCard::Speed
                || id == ESOLHudCard::DateAnchor;
            strong += HoloSmokeIsCardFrameless(stats) && stats.Strength == SOL::Settings::HOLO_MENU_EFFECTS_MAX
                && stats.Sweeps == 1 && stats.TickPadPx == SOL::Hologram::HUD_GLOW_PAD_PX
                && (!bIsAlwaysOn || stats.ContentPadPx == SOL::Hologram::HUD_GLOW_PAD_PX)
                && FMath::IsNearlyEqual(stats.BackingAlpha, 2.0f * BACKDROP_ALPHA_PER_UNIT) ? 1 : 0;
            detail += FString::Printf(TEXT("%s: %s; "), HoloSmokeCardName(id), *HoloSmokeCardText(stats));
        }
        Check(TEXT("c-strength2"), mIsCardExecuted && strong == static_cast<int32>(ESOLHudCard::Count),
            FString::Printf(TEXT("%d cards with the strength-2 look: %s"), strong, *detail));
        if (USOLTargetingSubsystem* targeting = pawn.GetWorld()->GetSubsystem<USOLTargetingSubsystem>())
        {
            targeting->ClearTarget();
        }
        mInteraction->ClearInfo();
        SetStrength(SOL::Settings::HOLO_MENU_EFFECTS_DEFAULT);
        break;
    }
    case EHoloStep::MapOpen:
        Check(TEXT("m-map-open"), pawn.IsMapOpen() && !pawn.IsActionListOpen(), FString::Printf(TEXT("jump map %s, "
            "list menu %s"), pawn.IsMapOpen() ? TEXT("open") : TEXT("CLOSED"), pawn.IsActionListOpen() ? TEXT("OPEN")
            : TEXT("closed")));
        break;
    case EHoloStep::ListOpen:
    {
        // Step 8: one open cue, no hover cue while it opened, the draw-in mid-way, then the full option (c) frame with
        // the hint divider (no tabs, no column divider) and the mark around the highlighted row
        const SSOLHoloFrame* listFrame = GetListFrame(pawn);
        const USOLActionListWidget* list = pawn.GetActionList();
        const int32 opens = holoMenu->GetCueRequestCount(ESOLHoloCue::Open) - mOpenRequestsBefore;
        const int32 hovers = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover) - mHoverRequestsBefore;
        const bool bIsOpen = listFrame != nullptr && listFrame->GetAnim().Phase == ESOLHoloMenuPhase::Open;
        const int32 outline = CountPart(ESOLHoloPart::Outline, listFrame);
        const int32 fullOutline = listFrame != nullptr ? FullOutlineCount(*listFrame) : -1;
        const int32 highlighted = list != nullptr ? list->GetState().Highlighted : INDEX_NONE;
        const int32 hoveredItem = listFrame != nullptr ? listFrame->GetAnim().HoveredItem : INDEX_NONE;
        const int32 tabParts = CountPart(ESOLHoloPart::Tab, listFrame) + CountPart(ESOLHoloPart::TabDot, listFrame)
            + CountPart(ESOLHoloPart::ColumnDivider, listFrame);
        Check(TEXT("m-list-open"), pawn.IsActionListOpen() && list != nullptr && bIsOpen && opens == 1 && hovers == 0
            && outline == fullOutline && outline > 0 && CountPart(ESOLHoloPart::GapDot, listFrame) > 0
            && CountPart(ESOLHoloPart::BrokenDivider, listFrame) > 0 && tabParts == 0
            && listFrame->GetLastPaintedBackingCount() == 1 && listFrame->GetMaterial() == menuMaterial
            && list->GetContentOpacity() == 1.0f && IsFrameOutputFresh(listFrame) && mListTargetRow != INDEX_NONE
            && highlighted == mListTargetRow && hoveredItem == highlighted
            && CountPart(ESOLHoloPart::Hover, listFrame) == HOVER_MAX_SEGMENTS
            && CountPart(ESOLHoloPart::HoverDot, listFrame) == 1,
            FString::Printf(TEXT("list %s with %d rows, phase %s, %d open / %d hover cue requests, outline %d of %d "
                "pieces, %d gap dots, %d hint divider pieces, %d tab and column pieces, %d backing, material %s, "
                "content opacity %.2f; Target row %d, highlighted %d, hovered item %d, %d hover pieces and %d dot"),
                pawn.IsActionListOpen() ? TEXT("open") : TEXT("CLOSED"), list != nullptr ? list->GetActions().Num : 0,
                bIsOpen ? TEXT("Open") : TEXT("NOT open"), opens, hovers, outline, fullOutline,
                CountPart(ESOLHoloPart::GapDot, listFrame), CountPart(ESOLHoloPart::BrokenDivider, listFrame), tabParts,
                listFrame != nullptr ? listFrame->GetLastPaintedBackingCount() : -1, listFrame != nullptr
                && listFrame->GetMaterial() == menuMaterial ? TEXT("menu MID") : TEXT("OTHER"), list != nullptr
                ? list->GetContentOpacity() : -1.0f, mListTargetRow, highlighted, hoveredItem,
                CountPart(ESOLHoloPart::Hover, listFrame), CountPart(ESOLHoloPart::HoverDot, listFrame)));
        Check(TEXT("m-list-mid-open"), mIsMidOpenShot && mMidOpenFraction > 0.0f && mMidOpenFraction < 1.0f
            && mMidOpenOpacity >= 0.0f && mMidOpenOpacity < 1.0f, FString::Printf(TEXT("frame fraction %.3f, content "
                "opacity %.3f at %.2f s after the open"), mMidOpenFraction, mMidOpenOpacity, HOLOSMOKE_MID_OPEN_S));
        break;
    }
    case EHoloStep::ListHover:
    {
        // Step 8: the mouse onto a row, then onto Target: two hover cues, the brackets and pointer at Target
        const SSOLHoloFrame* listFrame = GetListFrame(pawn);
        const USOLActionListWidget* list = pawn.GetActionList();
        const int32 hovers = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover) - mHoverRequestsBefore;
        const int32 highlighted = list != nullptr ? list->GetState().Highlighted : INDEX_NONE;
        const int32 hoveredItem = listFrame != nullptr ? listFrame->GetAnim().HoveredItem : INDEX_NONE;
        Check(TEXT("m-list-hover"), mListHoverRow != INDEX_NONE && hovers == 2 && highlighted == mListTargetRow
            && hoveredItem == highlighted && CountPart(ESOLHoloPart::Hover, listFrame) == HOVER_MAX_SEGMENTS
            && CountPart(ESOLHoloPart::HoverDot, listFrame) == 1, FString::Printf(TEXT("mouse onto row %d, then onto "
                "Target (row %d): %d hover cue requests (want 2), highlighted row %d, hovered item %d, %d hover pieces "
                "and %d dot"), mListHoverRow, mListTargetRow, hovers, highlighted, hoveredItem,
                CountPart(ESOLHoloPart::Hover, listFrame), CountPart(ESOLHoloPart::HoverDot, listFrame)));
        break;
    }
    case EHoloStep::ListSelect:
    {
        // Step 8: Enter on an enabled action: one select cue, the action ran, the map kept, the close played out
        const SSOLHoloFrame* listFrame = GetListFrame(pawn);
        const USOLActionListWidget* list = pawn.GetActionList();
        const USOLTargetingSubsystem* targeting = pawn.GetWorld()->GetSubsystem<USOLTargetingSubsystem>();
        const int32 selects = holoMenu->GetCueRequestCount(ESOLHoloCue::Select) - mSelectRequestsBefore;
        const int32 executed = mInteraction->GetExecutedCount() - mExecutedBefore;
        const bool bIsTargeted = targeting != nullptr && targeting->GetSelectedTarget() != nullptr;
        Check(TEXT("m-list-select"), selects == 1 && executed == 1 && bIsTargeted && pawn.IsMapOpen(),
            FString::Printf(TEXT("Enter on Target: %d select cue requests, %d actions run, target %s, map %s"), selects,
                executed, bIsTargeted ? TEXT("selected") : TEXT("NONE"), pawn.IsMapOpen() ? TEXT("open")
                : TEXT("CLOSED")));
        Check(TEXT("m-list-input-back"), mIsInputBackAtEsc, mEscDetail);
        const bool bIsGone = list != nullptr && !list->IsInViewport();
        const bool bIsClosed = listFrame == nullptr || listFrame->GetAnim().Phase == ESOLHoloMenuPhase::Closed;
        Check(TEXT("m-list-close"), mIsMidCloseInViewport && mMidClosePhase == ESOLHoloMenuPhase::Closing
            && mMidCloseFraction > 0.0f && mMidCloseFraction < 1.0f && mMidCloseOpacity >= 0.0f
            && mMidCloseOpacity < 1.0f && bIsGone && bIsClosed, FString::Printf(TEXT("mid-close: %s, phase %d, "
                "draw-in %.3f, content opacity %.3f; after %.2f s: widget %s, frame %s"), mIsMidCloseInViewport
                ? TEXT("drawn") : TEXT("NOT drawn"), static_cast<int32>(mMidClosePhase), mMidCloseFraction,
                mMidCloseOpacity, HOLOSMOKE_CLOSED_S, bIsGone ? TEXT("removed") : TEXT("STILL ON THE VIEWPORT"),
                bIsClosed ? TEXT("Closed") : TEXT("NOT closed")));
        break;
    }
    case EHoloStep::ListCancel:
    {
        // A disabled row and a cancel ask no select cue (SDD 5); Up onto the disabled row is a highlight change
        const USOLActionListWidget* list = pawn.GetActionList();
        const int32 opens = holoMenu->GetCueRequestCount(ESOLHoloCue::Open) - mOpenRequestsBefore;
        const int32 hovers = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover) - mHoverRequestsBefore;
        const int32 selects = holoMenu->GetCueRequestCount(ESOLHoloCue::Select) - mSelectRequestsBefore;
        const int32 executed = mInteraction->GetExecutedCount() - mExecutedBefore;
        const bool bIsDisabledRow = list != nullptr && mListRowAfterUp >= 0 && mListRowAfterUp < list->GetActions().Num
            && !list->GetActions().Entries[mListRowAfterUp].IsEnabled();
        const bool bIsGone = !pawn.IsActionListOpen() && list != nullptr && !list->IsInViewport();
        Check(TEXT("m-list-cancel"), mSubStep >= 3 && opens == 1 && hovers == 1 && selects == 0 && executed == 0
            && bIsDisabledRow && mIsListOpenAfterDisabled && bIsGone && pawn.IsMapOpen(), FString::Printf(TEXT("Up "
                "onto row %d (%s), Enter: list %s; Esc: list %s, map %s; %d open, %d hover and %d select cue requests "
                "(want 1, 1, 0), %d actions run"), mListRowAfterUp, bIsDisabledRow ? TEXT("disabled")
                : TEXT("NOT DISABLED"), mIsListOpenAfterDisabled ? TEXT("still open") : TEXT("CLOSED"), bIsGone
                ? TEXT("gone") : TEXT("STILL SHOWN"), pawn.IsMapOpen() ? TEXT("open") : TEXT("CLOSED"), opens, hovers,
                selects, executed));
        ClearTarget(pawn);
        break;
    }
    case EHoloStep::ListReopen:
    {
        // A reopen during the close (SDD 4.5): the close reverses with one open cue, the list keeps the viewport, takes
        // input and focus again, and closes normally after
        const USOLActionListWidget* list = pawn.GetActionList();
        const bool bIsGone = !pawn.IsActionListOpen() && list != nullptr && !list->IsInViewport();
        Check(TEXT("m-list-reopen"), mIsListClosingAtReopen && mListReopenOpens == 1 && mIsListReopened && bIsGone,
            FString::Printf(TEXT("reopened at %.2f s into the close (%s): %d open cue requests (want 1), %s; Esc "
                "after: list %s"), 0.5 * SOL::HoloMenu::CLOSE_S, mIsListClosingAtReopen ? TEXT("was closing")
                : TEXT("WAS NOT CLOSING"), mListReopenOpens, *mListReopenDetail, bIsGone ? TEXT("gone")
                : TEXT("STILL SHOWN")));
        break;
    }
    case EHoloStep::ListFlat:
    {
        // Step 9, the list menu at strength 0 (D18): whole and flat at once, the mark still moves; the cues asked (D21)
        const SSOLHoloFrame* listFrame = GetListFrame(pawn);
        const USOLActionListWidget* list = pawn.GetActionList();
        const int32 opens = holoMenu->GetCueRequestCount(ESOLHoloCue::Open) - mOpenRequestsBefore;
        const int32 hovers = holoMenu->GetCueRequestCount(ESOLHoloCue::Hover) - mHoverRequestsBefore;
        const FSOLHoloFrameOutput* output = listFrame != nullptr ? &listFrame->GetLastOutput() : nullptr;
        const float alpha = output != nullptr && output->bHasBacking ? output->Backing.Alpha : -1.0f;
        const int32 fullOutline = listFrame != nullptr ? FullOutlineCount(*listFrame) : -1;
        const int32 highlighted = list != nullptr ? list->GetState().Highlighted : INDEX_NONE;
        Check(TEXT("m-strength0-open"), mFirstFlatFraction == 1.0f && mMidOpenOpacity == 1.0f && opens == 1,
            FString::Printf(TEXT("first paint after the open: frame fraction %.3f, content opacity %.3f; %d open cue "
                "requests (D21)"), mFirstFlatFraction, mMidOpenOpacity, opens));
        Check(TEXT("m-strength0-flat"), listFrame != nullptr && listFrame->GetLastStrength() == 0.0f
            && CountPart(ESOLHoloPart::Sweep, listFrame) == 0 && alpha == FLAT_PANEL_ALPHA
            && AllLinePads(FRAME_FLAT_PAD_PX, listFrame) && CountPart(ESOLHoloPart::Outline, listFrame) == fullOutline
            && CountPart(ESOLHoloPart::Hover, listFrame) == HOVER_MAX_SEGMENTS
            && CountPart(ESOLHoloPart::BrokenDivider, listFrame) > 0,
            FString::Printf(TEXT("strength %.2f, %d sweeps, backing alpha %.2f, flat pads %s, outline %d of %d pieces, "
                "%d hover pieces, %d hint divider pieces"), listFrame != nullptr ? listFrame->GetLastStrength() : -1.0f,
                CountPart(ESOLHoloPart::Sweep, listFrame), alpha, AllLinePads(FRAME_FLAT_PAD_PX, listFrame)
                ? TEXT("yes") : TEXT("NO"), CountPart(ESOLHoloPart::Outline, listFrame), fullOutline,
                CountPart(ESOLHoloPart::Hover, listFrame), CountPart(ESOLHoloPart::BrokenDivider, listFrame)));
        Check(TEXT("m-strength0-hover"), hovers == 2 && highlighted == mListTargetRow && listFrame != nullptr
            && listFrame->GetAnim().HoveredItem == highlighted, FString::Printf(TEXT("Down and Up: %d hover cue "
                "requests (D21; want 2), highlighted row %d (want %d), hovered item %d"), hovers, highlighted,
                mListTargetRow, listFrame != nullptr ? listFrame->GetAnim().HoveredItem : INDEX_NONE));
        break;
    }
    case EHoloStep::ListFlatSelect:
    {
        // Strength 0 is fully usable (D18): Enter runs Target with the select cue (D21) and the close is instant
        const USOLTargetingSubsystem* targeting = pawn.GetWorld()->GetSubsystem<USOLTargetingSubsystem>();
        const int32 selects = holoMenu->GetCueRequestCount(ESOLHoloCue::Select) - mSelectRequestsBefore;
        const int32 executed = mInteraction->GetExecutedCount() - mExecutedBefore;
        const bool bIsTargeted = targeting != nullptr && targeting->GetSelectedTarget() != nullptr;
        Check(TEXT("m-strength0-select"), selects == 1 && executed == 1 && bIsTargeted && mIsFlatClosedAtOnce,
            FString::Printf(TEXT("Enter on Target at strength 0: %d select cue requests, %d actions run, target %s, "
                "widget %s the frame the list closed"), selects, executed, bIsTargeted ? TEXT("selected")
                : TEXT("NONE"), mIsFlatClosedAtOnce ? TEXT("removed") : TEXT("STILL ON THE VIEWPORT")));
        break;
    }
    case EHoloStep::ListStrong:
    {
        const SSOLHoloFrame* listFrame = GetListFrame(pawn);
        const FSOLHoloFrameOutput* output = listFrame != nullptr ? &listFrame->GetLastOutput() : nullptr;
        const float alpha = output != nullptr && output->bHasBacking ? output->Backing.Alpha : -1.0f;
        Check(TEXT("m-strength2"), listFrame != nullptr
            && listFrame->GetLastStrength() == SOL::Settings::HOLO_MENU_EFFECTS_MAX
            && CountPart(ESOLHoloPart::Sweep, listFrame) == 1
            && AllLinePads(SOL::Hologram::HUD_GLOW_PAD_PX, listFrame)
            && FMath::IsNearlyEqual(alpha, 2.0f * BACKDROP_ALPHA_PER_UNIT),
            FString::Printf(TEXT("strength %.2f, %d sweep, glow pads %s, backing alpha %.2f"), listFrame != nullptr
                ? listFrame->GetLastStrength() : -1.0f, CountPart(ESOLHoloPart::Sweep, listFrame),
                AllLinePads(SOL::Hologram::HUD_GLOW_PAD_PX, listFrame) ? TEXT("yes") : TEXT("NO"), alpha));
        SlateKey(EKeys::Escape, FModifierKeysState());
        SetStrength(SOL::Settings::HOLO_MENU_EFFECTS_DEFAULT);
        break;
    }
    case EHoloStep::MapClose:
    {
        const USOLActionListWidget* list = pawn.GetActionList();
        Check(TEXT("m-map-close"), !pawn.IsMapOpen() && !pawn.IsActionListOpen() && list != nullptr
            && !list->IsInViewport() && mIsListClosingAtMapClose && mIsListGoneAtMapClose,
            FString::Printf(TEXT("map %s, list menu %s; list %s when the map closed, %s"), pawn.IsMapOpen()
                ? TEXT("OPEN") : TEXT("closed"), list != nullptr && list->IsInViewport()
                ? TEXT("STILL ON THE VIEWPORT") : TEXT("gone"), mIsListClosingAtMapClose ? TEXT("still closing")
                : TEXT("NOT CLOSING (nothing to tell apart)"), *mMapCloseDetail));
        ClearTarget(pawn);
        break;
    }
    case EHoloStep::Quit:
        UE_LOG(LogSOL, Log, TEXT("HoloMenu SUMMARY: %d of %d checks passed (parts 42c-42g; the Settings step is a "
            "stub)"), mChecksPassed, mChecksRun);
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
// Once the step's time is up, reports done a few frames later: the HUD's card stats are those of its last draw, which
// lags the wall clock by a whole frame after a slow (screenshot) frame, so the checks read a draw from past that time
bool FSOLHoloMenuSmoke::HudDrawnThenDone(const bool bIsTimeUp)
{
    if (!bIsTimeUp)
    {
        return false;
    }
    if (mTimeUpFrame == INDEX_NONE)
    {
        mTimeUpFrame = mStepFrames;
    }
    return mStepFrames >= mTimeUpFrame + HOLOSMOKE_HUD_FRAMES;
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
// Returns the map list menu's Slate frame, or null (before its first open)
SSOLHoloFrame* FSOLHoloMenuSmoke::GetListFrame(const ASOLShipPawn& pawn)
{
    const USOLActionListWidget* list = pawn.GetActionList();
    const USOLHoloFrameWidget* frameWidget = list != nullptr ? list->GetHoloFrame() : nullptr;
    return frameWidget != nullptr && frameWidget->GetFrame().IsValid() ? frameWidget->GetFrame().Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Opens the map's list menu on the radial body at a point of the view and finds its Target row
void FSOLHoloMenuSmoke::OpenListOnBody(ASOLShipPawn& pawn, const FVector2D& viewFraction)
{
    int32 width = 0;
    int32 height = 0;
    if (mController.IsValid())
    {
        mController->GetViewportSize(width, height);
    }
    const bool bIsOpen = pawn.OpenActionList(mRadialBody, FVector2D(width * viewFraction.X,
        height * viewFraction.Y));
    mListTargetRow = INDEX_NONE;
    const USOLActionListWidget* list = pawn.GetActionList();
    for (int32 row = 0; list != nullptr && row < list->GetActions().Num; ++row)
    {
        mListTargetRow = list->GetActions().Entries[row].Action == ESOLAction::Target ? row : mListTargetRow;
    }
    UE_LOG(LogSOL, Log, TEXT("HoloMenu: list menu on body %d %s, Target row %d"), mRadialBody.Index, bIsOpen
        ? TEXT("opened") : TEXT("NOT opened"), mListTargetRow);
}

//////////////////////////////////////////////////////////////////////////
// Runs the list reopen step: Esc once Open, the reopen at another point half a close later (SDD 4.5: the close
// reverses), the state a key gap after it, then Esc and the close played out; returns true once done
bool FSOLHoloMenuSmoke::TickListReopen(ASOLShipPawn& pawn, const double elapsedS)
{
    const USOLActionListWidget* list = pawn.GetActionList();
    if (list == nullptr || !mHoloMenu.IsValid())
    {
        return elapsedS >= HOLOSMOKE_OVERLAY_TIMEOUT_S;
    }
    if (mSubStep == 0 && elapsedS >= HOLOSMOKE_OPENED_S)
    {
        SlateKey(EKeys::Escape, FModifierKeysState());
        mSubStep = 1;
        mSubStepS = elapsedS;
    }
    else if (mSubStep == 1 && elapsedS >= mSubStepS + 0.5 * SOL::HoloMenu::CLOSE_S)
    {
        // Mid-close: the list still on the viewport and closing; reopened at another point
        mIsListClosingAtReopen = !pawn.IsActionListOpen() && list->IsClosing() && list->IsInViewport();
        mOpenRequestsBefore = mHoloMenu->GetCueRequestCount(ESOLHoloCue::Open);
        OpenListOnBody(pawn, HOLOSMOKE_LIST_REOPEN_AT);
        mSubStep = 2;
        mSubStepS = elapsedS;
    }
    else if (mSubStep == 2 && elapsedS >= mSubStepS + HOLOSMOKE_KEY_GAP_S)
    {
        // A key gap later: one open cue, on the viewport, taking input again and focused; then Esc
        mListReopenOpens = mHoloMenu->GetCueRequestCount(ESOLHoloCue::Open) - mOpenRequestsBefore;
        const bool bIsFocused = list->HasAnyUserFocus() || list->HasFocusedDescendants();
        const bool bIsHitTestable = list->GetVisibility() != ESlateVisibility::HitTestInvisible;
        mIsListReopened = pawn.IsActionListOpen() && list->IsInViewport() && !list->IsClosing()
            && list->IsFocusable() && bIsFocused && bIsHitTestable;
        mListReopenDetail = FString::Printf(TEXT("pawn list %s, widget %s and %s, %s, %s, %s"),
            pawn.IsActionListOpen() ? TEXT("open") : TEXT("CLOSED"), list->IsInViewport() ? TEXT("on the viewport")
            : TEXT("REMOVED"), list->IsClosing() ? TEXT("STILL CLOSING") : TEXT("not closing"), list->IsFocusable()
            ? TEXT("focusable") : TEXT("NOT FOCUSABLE"), bIsFocused ? TEXT("focused") : TEXT("NOT FOCUSED"),
            bIsHitTestable ? TEXT("hit-testable") : TEXT("HIT-TEST INVISIBLE"));
        SlateKey(EKeys::Escape, FModifierKeysState());
        mSubStep = 3;
        mSubStepS = elapsedS;
    }
    return mSubStep >= 3 && elapsedS >= mSubStepS + HOLOSMOKE_CLOSED_S;
}

//////////////////////////////////////////////////////////////////////////
// Runs the map close step: closes the map once the list is closing (or after a key gap), then records the first frame
// the map is closed; returns true once the close time has passed
bool FSOLHoloMenuSmoke::TickMapClose(ASOLShipPawn& pawn, const double elapsedS)
{
    if (mSubStep == 0)
    {
        const USOLActionListWidget* list = pawn.GetActionList();
        mIsListClosingAtMapClose = list != nullptr && list->IsInViewport() && list->IsClosing();
        if (!mIsListClosingAtMapClose && elapsedS < HOLOSMOKE_KEY_GAP_S)
        {
            return false;
        }
        pawn.HandleCloseMap();
        mSubStep = 1;
        mSubStepS = elapsedS;
    }
    RecordMapCloseFrame(pawn);
    return mSubStep >= 1 && elapsedS >= mSubStepS + HOLOSMOKE_CLOSED_S;
}

//////////////////////////////////////////////////////////////////////////
// Records, on the first frame the map is closed, whether the list menu is already off the viewport and Closed (the
// map's close dismisses it at once rather than letting its close end on its own)
void FSOLHoloMenuSmoke::RecordMapCloseFrame(const ASOLShipPawn& pawn)
{
    if (mSubStep != 1 || pawn.IsMapOpen())
    {
        return;
    }
    mSubStep = 2;
    const USOLActionListWidget* list = pawn.GetActionList();
    const SSOLHoloFrame* listFrame = GetListFrame(pawn);
    const bool bIsOff = list != nullptr && !list->IsInViewport();
    // Removing the widget releases its Slate frame, so a released frame counts as Closed (the next open builds it anew)
    const bool bIsClosed = listFrame == nullptr || listFrame->GetAnim().Phase == ESOLHoloMenuPhase::Closed;
    mIsListGoneAtMapClose = bIsOff && bIsClosed;
    mMapCloseDetail = FString::Printf(TEXT("first frame with the map closed: list %s, frame %s"), bIsOff
        ? TEXT("off the viewport") : TEXT("STILL ON THE VIEWPORT"), listFrame == nullptr
        ? TEXT("released with the widget") : (bIsClosed ? TEXT("Closed") : TEXT("NOT CLOSED")));
}

//////////////////////////////////////////////////////////////////////////
// Clears the selected target (the list's Target row is enabled again)
void FSOLHoloMenuSmoke::ClearTarget(const ASOLShipPawn& pawn)
{
    if (USOLTargetingSubsystem* targeting = pawn.GetWorld()->GetSubsystem<USOLTargetingSubsystem>())
    {
        targeting->ClearTarget();
    }
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
