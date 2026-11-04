/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHoloMenuSmoke.h"

#include "Menu/SOLSettingsSubsystem.h"
#include "Ship/SOLShipPawn.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "UI/SOLFlightHud.h"
#include "UI/SOLHoloFrame.h"
#include "UI/SOLHoloFrameWidget.h"
#include "UI/SOLHoloMenuSubsystem.h"
#include "UI/SOLHologramCursorSubsystem.h"

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
        Stubs,          // The section 12 checks later parts fill in
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
    UE_LOG(LogSOL, Log, TEXT("HoloMenu: scripted hologram menu checks starting (part 42c scaffold)"));
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
    if (!mController.IsValid() || !mHoloMenu.IsValid() || !mSettings.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("HoloMenu: controller, menu or settings subsystem missing; aborting"));
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
    case EHoloStep::Quit:
        UE_LOG(LogSOL, Log, TEXT("HoloMenu SUMMARY: %d of %d checks passed (part 42c scaffold; menu steps are stubs)"),
            mChecksPassed, mChecksRun);
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
// Logs the section 12 checks later parts fill in
void FSOLHoloMenuSmoke::LogStubs()
{
    // SDD 42 section 12: the step, what it will check and the part that adds it
    static const TCHAR* const STUBS[] = {
        TEXT("1 Ctrl+O: frame widget, Open after OPEN_S with its full outline, one open cue (42e)"),
        TEXT("2 Ctrl+O mid-open capture at ~0.1 s, frame fraction in (0, 1) (42e)"),
        TEXT("3 hover a row, then Down: at most two hover cues, brackets and pointer at the row (42e)"),
        TEXT("4 toggle a row: one select cue; Esc: input back at once, widget gone after CLOSE_S (42e)"),
        TEXT("5 radial: draw-in, hover cues across two sectors, select cue, close from the cached state (42d)"),
        TEXT("6 F3: open, digit step (select cue), highlight move (hover cue), close (42f)"),
        TEXT("7 info / target / speed / date-and-anchor cards on the menu layer, 8 ticks each (42g)"),
        TEXT("8 map list menu: open, hover, select cues (42g)"),
        TEXT("9 strength 0 in the menus: instant open, flat look, still usable, cues requested (42d-42g)"),
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
