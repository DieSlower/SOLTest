/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLHologramSmoke.h"

#include "GasGiants/SOLGasGiantVisuals.h"
#include "Game/SOLGameMode.h"
#include "Map/SOLMapModeSubsystem.h"
#include "MinorBodies/SOLRingVisuals.h"
#include "Ship/SOLShipPawn.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "UI/SOLFlightHud.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Visuals/SOLBodyVisuals.h"
#include "Visuals/SOLHologramVisuals.h"

#include "Engine/Engine.h"
#include "RHI.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

namespace
{
    constexpr double HOLO_SMOKE_SETTLE_S = 3.0;
    constexpr double HOLO_SMOKE_VIEW_WAIT_S = 1.0;
    constexpr int32 HOLO_SMOKE_SHORT_FRAMES = 6;
    // A screenshot at the viewport's own size (aspect ratio), unlike ASOLGameMode::CaptureScreenshot's fixed 16:9
    const TCHAR* const HOLO_SMOKE_SCREENSHOT_COMMAND = TEXT("HighResShot 1");
    constexpr int32 HOLO_SMOKE_GPU_FRAMES = 240;          // Frames averaged per GPU measurement

    // Screenshot tour: focus body, camera distance (m), pitch and yaw (rad)
    struct FHoloSmokeView
    {
        const TCHAR* Body;
        double DistanceM;
        double PitchRad;
        double YawRad;
    };
    const FHoloSmokeView HOLO_SMOKE_VIEWS[] =
    {
        { TEXT("Earth"), 1.2e9, 0.5, 0.3 },
        { TEXT("Earth"), 3.0e7, 0.35, 1.0 },
        { TEXT("Jupiter"), 4.5e9, 0.6, -0.4 },
        { TEXT("Sun"), 6.0e9, 0.4, 0.0 },
        { TEXT("Sun"), 6.0e11, 0.7, 0.8 },
        { TEXT("Saturn"), 8.0e8, 0.45, 0.6 },
    };
    constexpr int32 HOLO_SMOKE_VIEW_COUNT = UE_ARRAY_COUNT(HOLO_SMOKE_VIEWS);

    // Script steps
    enum EHoloStep : int32
    {
        HoloStepSettle,
        HoloStepOpen,
        HoloStepSwapCheck,
        HoloStepTour,
        HoloStepMeasureOn,
        HoloStepCvarOff,
        HoloStepMeasureOff,
        HoloStepCvarOn,
        HoloStepClose,
        HoloStepDone,
    };

    //////////////////////////////////////////////////////////////////////////
    // Sets sol.Hologram
    void HoloSmokeSetEnabled(const bool bEnabled)
    {
        if (IConsoleVariable* cvar = IConsoleManager::Get().FindConsoleVariable(SOL::Hologram::CVAR_ENABLED))
        {
            cvar->Set(bEnabled ? 1 : 0, ECVF_SetByCode);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Caches the controller, subsystems and visuals actors
void FSOLHologramSmoke::Start(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    mController = pawn.GetController<APlayerController>();
    mMapMode = world->GetSubsystem<USOLMapModeSubsystem>();
    mRegistry = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    for (TActorIterator<ASOLHologramVisuals> iterator(world); iterator; ++iterator)
    {
        mHologram = *iterator;
    }
    for (TActorIterator<ASOLBodyVisuals> iterator(world); iterator; ++iterator)
    {
        mBodyVisuals = *iterator;
    }

    // The body visuals plus one actor per ring and per gas giant
    for (TActorIterator<AActor> iterator(world); iterator; ++iterator)
    {
        mExpectedHidden += iterator->IsA<ASOLBodyVisuals>() || iterator->IsA<ASOLRingVisuals>()
            || iterator->IsA<ASOLGasGiantVisuals>() ? 1 : 0;
    }
    UE_LOG(LogSOL, Log, TEXT("SmokeHologram: map hologram run starting"));
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script (from the pawn's tick); returns true once it has finished
bool FSOLHologramSmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    if (mIsFinished)
    {
        return true;
    }
    ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>();
    const ASOLFlightHud* hud = mController.IsValid() ? mController->GetHUD<ASOLFlightHud>() : nullptr;
    if (gameMode == nullptr || hud == nullptr || !mMapMode.IsValid() || !mRegistry.IsValid() || !mHologram.IsValid()
        || !mBodyVisuals.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeHologram: controller, HUD, subsystems or visuals missing; aborting"));
        mIsFinished = true;
        if (gameMode != nullptr)
        {
            gameMode->TakeSmokeScreenshot();
        }
        return true;
    }
    mStepTimeS += realDeltaSeconds;
    ++mStepFrames;
    if (mMapMode->IsMapOpen())
    {
        AimView();
    }
    const ASOLHologramVisuals& hologram = *mHologram;
    const FSOLMapHologramOverlay& overlay = hud->GetHologramOverlay();
    const int32 proxies = hologram.GetProxyCount();
    const int32 expectedHidden = mExpectedHidden;

    switch (mStep)
    {
    case HoloStepSettle:
        if (mStepTimeS >= HOLO_SMOKE_SETTLE_S)
        {
            mHiddenMeshesBefore = CountHiddenBodyMeshes();
            Check(TEXT("hidden before the map"), !hologram.IsShown() && hologram.CountVisibleProxies() == 0
                && CountHiddenRealActors() == 0 && !hologram.IsPostProcessEnabled(), FString::Printf(
                TEXT("%d proxies visible, %d real actors hidden, %d placeholder meshes hidden by their owners"),
                hologram.CountVisibleProxies(), CountHiddenRealActors(), mHiddenMeshesBefore));
            mView = 0;
            mMapMode->OpenMap(mController.Get());
            NextStep();
        }
        break;

    case HoloStepOpen:
        if (mStepTimeS >= HOLO_SMOKE_VIEW_WAIT_S)
        {
            NextStep();
        }
        break;

    case HoloStepSwapCheck:
        Check(TEXT("proxy swap on open"), hologram.IsShown() && proxies > 0
            && hologram.CountVisibleProxies() == proxies && hologram.GetHiddenActorCount() == CountHiddenRealActors()
            && CountHiddenRealActors() == expectedHidden && expectedHidden > 0, FString::Printf(
            TEXT("%d/%d proxies visible, %d real actors hidden"), hologram.CountVisibleProxies(), proxies,
            CountHiddenRealActors()));
        Check(TEXT("post-process on"), hologram.IsPostProcessEnabled(), TEXT(""));
        NextStep();
        break;

    case HoloStepTour:
        if (mStepTimeS >= HOLO_SMOKE_VIEW_WAIT_S)
        {
            const FHoloSmokeView& view = HOLO_SMOKE_VIEWS[mView];
            const FString name = FString::Printf(TEXT("grid drawn (%s at %.3g m)"), view.Body, view.DistanceM);
            Check(*name, overlay.GetLastGridLines() > 0, FString::Printf(
                TEXT("%d grid lines, minor %.3g m, %d stems, %d triangles"), overlay.GetLastGridLines(),
                overlay.GetLastMinorSpacingM(), overlay.GetLastStems(), overlay.GetLastTriangles()));
            UE_LOG(LogSOL, Log, TEXT("SmokeHologram: screenshot (view %d: %s at %.3g m)"), mView, view.Body,
                view.DistanceM);
            mController->ConsoleCommand(HOLO_SMOKE_SCREENSHOT_COMMAND);
            ++mView;
            mStepTimeS = 0.0;
            if (mView >= HOLO_SMOKE_VIEW_COUNT)
            {
                // Keep the last view for the cvar steps and the GPU measurement
                mView = HOLO_SMOKE_VIEW_COUNT - 1;
                NextStep();
            }
        }
        break;

    case HoloStepMeasureOn:
    case HoloStepMeasureOff:
        // Mean GPU frame time over a fixed frame count, with the look on and then off (same view)
        if (mStepFrames > HOLO_SMOKE_SHORT_FRAMES)
        {
            mGpuMsSum += FPlatformTime::ToMilliseconds64(RHIGetGPUFrameCycles());
            ++mGpuSamples;
        }
        if (mGpuSamples >= HOLO_SMOKE_GPU_FRAMES)
        {
            const double meanMs = mGpuMsSum / mGpuSamples;
            UE_LOG(LogSOL, Log, TEXT("SmokeHologram: GPU frame %.3f ms (mean of %d frames, look %s)"), meanMs,
                mGpuSamples, mStep == HoloStepMeasureOn ? TEXT("on") : TEXT("off"));
            mGpuMsSum = 0.0;
            mGpuSamples = 0;
            NextStep();
        }
        break;

    case HoloStepCvarOff:
        // Wait for the last screenshot to be captured before the look changes
        if (mStepFrames == HOLO_SMOKE_SHORT_FRAMES)
        {
            HoloSmokeSetEnabled(false);
        }
        if (mStepFrames >= 2 * HOLO_SMOKE_SHORT_FRAMES)
        {
            Check(TEXT("sol.Hologram 0 restores the real visuals"), !hologram.IsShown()
                && hologram.CountVisibleProxies() == 0 && CountHiddenRealActors() == 0
                && !hologram.IsPostProcessEnabled() && overlay.GetLastGridLines() == 0
                && CountHiddenBodyMeshes() == mHiddenMeshesBefore, FString::Printf(
                TEXT("%d proxies visible, %d real actors hidden, %d grid lines, %d placeholders hidden"),
                hologram.CountVisibleProxies(), CountHiddenRealActors(), overlay.GetLastGridLines(),
                CountHiddenBodyMeshes()));
            UE_LOG(LogSOL, Log, TEXT("SmokeHologram: screenshot (map, sol.Hologram 0)"));
            mController->ConsoleCommand(HOLO_SMOKE_SCREENSHOT_COMMAND);
            NextStep();
        }
        break;

    case HoloStepCvarOn:
        if (mStepFrames == HOLO_SMOKE_SHORT_FRAMES)
        {
            HoloSmokeSetEnabled(true);
        }
        if (mStepFrames >= 2 * HOLO_SMOKE_SHORT_FRAMES)
        {
            Check(TEXT("sol.Hologram 1 shows it again"), hologram.IsShown()
                && hologram.CountVisibleProxies() == proxies && CountHiddenRealActors() >= expectedHidden,
                FString::Printf(TEXT("%d proxies visible, %d real actors hidden"), hologram.CountVisibleProxies(),
                CountHiddenRealActors()));
            mMapMode->CloseMap(mController.Get());
            NextStep();
        }
        break;

    case HoloStepClose:
        if (mStepFrames >= HOLO_SMOKE_SHORT_FRAMES)
        {
            Check(TEXT("close restores the real visuals"), !mMapMode->IsMapOpen() && !hologram.IsShown()
                && hologram.CountVisibleProxies() == 0 && CountHiddenRealActors() == 0
                && hologram.GetHiddenActorCount() == 0 && !hologram.IsPostProcessEnabled()
                && CountHiddenBodyMeshes() == mHiddenMeshesBefore, FString::Printf(
                TEXT("%d proxies visible, %d real actors hidden, %d placeholders hidden (before %d)"),
                hologram.CountVisibleProxies(), CountHiddenRealActors(), CountHiddenBodyMeshes(),
                mHiddenMeshesBefore));
            UE_LOG(LogSOL, Log, TEXT("SmokeHologram: complete, %d/%d checks passed%s"), mChecks - mFailures,
                mChecks, mFailures == 0 ? TEXT("") : TEXT(" (FAILURES)"));
            mIsFinished = true;
            gameMode->TakeSmokeScreenshot();
            return true;
        }
        break;

    default:
        break;
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Points the map camera at view index mView
void FSOLHologramSmoke::AimView() const
{
    const FHoloSmokeView& view = HOLO_SMOKE_VIEWS[FMath::Clamp(mView, 0, HOLO_SMOKE_VIEW_COUNT - 1)];
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    const int32 body = registry.FindByName(FName(view.Body));
    if (body == INDEX_NONE)
    {
        return;
    }
    FSOLOrbitCameraState state;
    state.FocusPositionM = registry.GetPositionM(body);
    state.DistanceM = view.DistanceM;
    state.PitchRad = view.PitchRad;
    state.YawRad = view.YawRad;
    mMapMode->SetCameraState(state);
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of replaced real actors in the controller's HiddenActors
int32 FSOLHologramSmoke::CountHiddenRealActors() const
{
    int32 count = 0;
    if (mController.IsValid())
    {
        for (const TObjectPtr<AActor>& actor : mController->HiddenActors)
        {
            count += actor != nullptr && (actor->IsA<ASOLBodyVisuals>() || actor->IsA<ASOLRingVisuals>()
                || actor->IsA<ASOLGasGiantVisuals>()) ? 1 : 0;
        }
    }
    return count;
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of hidden body placeholder meshes
int32 FSOLHologramSmoke::CountHiddenBodyMeshes() const
{
    int32 count = 0;
    const int32 bodyCount = mRegistry->GetRegistry().Num();
    for (int32 index = 0; index < bodyCount; ++index)
    {
        count += mBodyVisuals->IsBodyMeshHidden(index) ? 1 : 0;
    }
    return count;
}

//////////////////////////////////////////////////////////////////////////
// Advances to the next step
void FSOLHologramSmoke::NextStep()
{
    ++mStep;
    mStepFrames = 0;
    mStepTimeS = 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Logs one checkpoint as PASS or FAIL and counts failures
void FSOLHologramSmoke::Check(const TCHAR* name, const bool bPassed, const FString& detail)
{
    ++mChecks;
    if (!bPassed)
    {
        ++mFailures;
    }
    UE_LOG(LogSOL, Log, TEXT("SmokeHologram: %s %s: %s"), bPassed ? TEXT("PASS") : TEXT("FAIL"), name, *detail);
}
