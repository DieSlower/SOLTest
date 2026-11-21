/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Ship/SOLPlayerThrusterSmoke.h"

#include "Flight/SOLFlightMode.h"
#include "Game/SOLGameMode.h"
#include "OrbitLines/SOLOrbitLineVisuals.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Visuals/SOLThrusterMath.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RenderTimer.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

namespace
{
    // State durations (real seconds) and frame counts
    constexpr double PTSMOKE_SETTLE_S = 3.0;                 // The world, camera swing and manual mode settle
    constexpr double PTSMOKE_STATE_S = 1.5;                  // Each thrust state settles this long before its checks
    constexpr double PTSMOKE_BOOST_S = 0.7;                  // Boost is short (9 G blacks the pilot out in seconds)
    constexpr double PTSMOKE_SWITCH_S = 0.3;                 // Override and cvar states
    constexpr double PTSMOKE_CAMERA_S = 0.8;                 // The camera eases back behind the ship (lag 8/s)
    constexpr double PTSMOKE_WAR_TIMEOUT_S = 30.0;           // A War power state waits at most this for its pulse
    constexpr int32 PTSMOKE_MIN_FRAMES = 5;
    constexpr double PTSMOKE_SHOT_HOLD_S = 0.4;              // A state's inputs hold this long after its screenshot

    // Script values
    constexpr double PTSMOKE_NORMAL_THRUST = 1.0 / 3.0;      // Manual forward input for about 1 G (3 G at full)
    constexpr double PTSMOKE_SWING_YAW_PX = 500.0;           // Free-look swing: 100 degrees to the side...
    constexpr double PTSMOKE_SWING_PITCH_PX = -60.0;         // ...and 12 degrees down onto the ship
    constexpr double PTSMOKE_WAR_PULSE_G = 6.0;              // War power: a pulse reads above this...
    constexpr double PTSMOKE_WAR_RECOVER_LOW_G = 1.0;        // ...a recovery between these
    constexpr double PTSMOKE_WAR_RECOVER_HIGH_G = 2.0;
    constexpr float PTSMOKE_FLOAT_TOLERANCE = 1.0e-4f;       // Q1: pushed against computed floats
    constexpr double PTSMOKE_PLUME_TOLERANCE = 1.0e-3;       // Q2: c and b against SOLThruster of the step's G
    constexpr double PTSMOKE_LENGTH_TOLERANCE = 1.0e-2;      // Q2: plume length (m)
    constexpr double PTSMOKE_LIGHT_TOLERANCE = 1.0e-2;       // Q3: light intensity (cd)
    constexpr float PTSMOKE_COLOUR_TOLERANCE = 0.01f;        // Q3: light colour (the engine stores it as an 8-bit sRGB
                                                             // FColor, so about 1/255 off the linear ramp)
    constexpr int32 PTSMOKE_LOW_PRESET = 0;                  // QualityPreset Low

    // The profile
    constexpr int32 PTSMOKE_PROFILE_PAIRS = 4;               // A/B pairs, ordered A B B A A B B A
    constexpr int32 PTSMOKE_PROFILE_SETTLE_FRAMES = 60;      // After each switch
    constexpr int32 PTSMOKE_PROFILE_FRAMES = 300;            // Measured frames per window
    constexpr double PTSMOKE_PROFILE_WAR_S = 8.0;            // The War power order flies this long before the windows
    constexpr float PTSMOKE_PROFILE_MAX_WINDOW_PX = 16384.0f;    // Lifted window size limit

    //////////////////////////////////////////////////////////////////////////
    // Sets an integer console variable, logging a name that does not exist
    void PlayerThrusterSmokeSetCvar(const TCHAR* name, const int32 value)
    {
        if (IConsoleVariable* cvar = IConsoleManager::Get().FindConsoleVariable(name))
        {
            cvar->Set(value, ECVF_SetByCode);
            return;
        }
        UE_LOG(LogSOL, Warning, TEXT("SmokePlayerThruster: console variable %s not found"), name);
    }

    //////////////////////////////////////////////////////////////////////////
    // Sets the -ResX / -ResY view past the desktop size with r.SetRes (as the NPC plume profile does); nothing without
    // both
    void PlayerThrusterSmokeApplyCommandLineView(UWorld* world)
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
        window->SetSizeLimits(FWindowSizeLimits().SetMaxWidth(PTSMOKE_PROFILE_MAX_WINDOW_PX)
            .SetMaxHeight(PTSMOKE_PROFILE_MAX_WINDOW_PX));
        GEngine->Exec(world, *FString::Printf(TEXT("r.SetRes %dx%dw"), width, height));
    }
}

//////////////////////////////////////////////////////////////////////////
// Caches the subsystems, sets up the overrides and the camera, and starts the first state
void FSOLPlayerThrusterSmoke::Start(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    const ASOLGameMode* gameMode = world->GetAuthGameMode<ASOLGameMode>();
    mOrbitLines = gameMode != nullptr ? gameMode->GetOrbitLineVisuals() : nullptr;
    mShips = world->GetSubsystem<USOLShipSubsystem>();
    mRegistry = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    mMarsIndex = mRegistry.IsValid() ? mRegistry->GetRegistry().FindByName(FName(SOL::BodyNames::MARS)) : INDEX_NONE;
    mIsProfile = FParse::Param(FCommandLine::Get(), SOL::CommandLine::SMOKE_PLAYER_THRUSTER_PROFILE) && !GIsEditor;
    UE_LOG(LogSOL, Log, TEXT("SmokePlayerThruster: starting (Mars index %d, profile %s)"), mMarsIndex,
        mIsProfile ? TEXT("on") : TEXT("off"));

    // Overrides in memory only: no space dust, no orbit lines, the player plume on
    if (mOrbitLines.IsValid())
    {
        mOrbitLines->BeginVerificationOverride();
        mOrbitLines->SetLinesEnabled(false);
    }
    PlayerThrusterSmokeSetCvar(SOL::SpaceDust::CVAR_ENABLED, 0);
    PlayerThrusterSmokeSetCvar(SOL::PLAYER_PLUME_CVAR, 1);
    mPhase = ESOLPlayerThrusterPhase::Settle;
    EnterPhase(pawn, mPhase);
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script; returns true once it has finished
bool FSOLPlayerThrusterSmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    if (mPhase == ESOLPlayerThrusterPhase::Done)
    {
        return true;
    }
    if (!mShips.IsValid() || !mRegistry.IsValid() || mMarsIndex == INDEX_NONE)
    {
        UE_LOG(LogSOL, Error, TEXT("SmokePlayerThruster: subsystems or Mars missing; aborting"));
        mPhase = ESOLPlayerThrusterPhase::Done;
        Finish(pawn);
        return true;
    }
    mPhaseTimeS += realDeltaSeconds;
    mTotalTimeS += realDeltaSeconds;
    ++mPhaseFrames;

    // Held inputs are applied every frame (the pawn composes its control from them)
    const bool bThrusting = mPhase >= ESOLPlayerThrusterPhase::Normal && mPhase <= ESOLPlayerThrusterPhase::CvarOff;
    pawn.HandleThrust(FVector3d(bThrusting ? (mPhase == ESOLPlayerThrusterPhase::Normal ? PTSMOKE_NORMAL_THRUST : 1.0)
        : 0.0, 0.0, 0.0));

    // A screenshot was requested: the state's inputs hold until it is written, then the next state starts
    if (mShotHoldS > 0.0)
    {
        mShotHoldS -= realDeltaSeconds;
        if (mShotHoldS > 0.0)
        {
            return false;
        }
    }
    else
    {
        if (mPhase == ESOLPlayerThrusterPhase::Profile)
        {
            if (!TickProfile(pawn.GetWorld(), realDeltaSeconds))
            {
                return false;
            }
        }
        else if (!IsPhaseDone(mPhase))
        {
            return false;
        }
        ExitPhase(pawn, mPhase);
        if (mShotHoldS > 0.0)
        {
            return false;
        }
    }

    if (mPhase == ESOLPlayerThrusterPhase::Settle && mIsProfile)
    {
        mPhase = ESOLPlayerThrusterPhase::Profile;
    }
    else if (mPhase == ESOLPlayerThrusterPhase::WarRecover || mPhase == ESOLPlayerThrusterPhase::Profile)
    {
        mPhase = ESOLPlayerThrusterPhase::Done;
    }
    else
    {
        mPhase = static_cast<ESOLPlayerThrusterPhase>(static_cast<uint8>(mPhase) + 1);
    }
    mPhaseTimeS = 0.0;
    mPhaseFrames = 0;
    if (mPhase == ESOLPlayerThrusterPhase::Done)
    {
        Finish(pawn);
        return true;
    }
    EnterPhase(pawn, mPhase);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a state may end now: its time and frames, and for War power the pulse or recovery it waits for
bool FSOLPlayerThrusterSmoke::IsPhaseDone(const ESOLPlayerThrusterPhase phase) const
{
    if (mPhaseFrames < PTSMOKE_MIN_FRAMES)
    {
        return false;
    }
    const double gForward = mShips->GetThrustLocalG().X;
    switch (phase)
    {
    case ESOLPlayerThrusterPhase::Settle:
        return mPhaseTimeS >= PTSMOKE_SETTLE_S;
    case ESOLPlayerThrusterPhase::Boost:
        return mPhaseTimeS >= PTSMOKE_BOOST_S;
    case ESOLPlayerThrusterPhase::LowQuality:
        return mPhaseTimeS >= PTSMOKE_CAMERA_S;
    case ESOLPlayerThrusterPhase::VrLow:
    case ESOLPlayerThrusterPhase::CvarOff:
        return mPhaseTimeS >= PTSMOKE_SWITCH_S;
    case ESOLPlayerThrusterPhase::WarPower:
        return (mPhaseTimeS >= PTSMOKE_STATE_S && gForward > PTSMOKE_WAR_PULSE_G)
            || mPhaseTimeS >= PTSMOKE_WAR_TIMEOUT_S;
    case ESOLPlayerThrusterPhase::WarRecover:
        return (gForward > PTSMOKE_WAR_RECOVER_LOW_G && gForward < PTSMOKE_WAR_RECOVER_HIGH_G)
            || mPhaseTimeS >= PTSMOKE_WAR_TIMEOUT_S;
    default:
        return mPhaseTimeS >= PTSMOKE_STATE_S;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a state's start: manual mode and the camera swing, boost, the overrides, the cvar, the War power order
void FSOLPlayerThrusterSmoke::EnterPhase(ASOLShipPawn& pawn, const ESOLPlayerThrusterPhase phase)
{
    UE_LOG(LogSOL, Log, TEXT("SmokePlayerThruster: state %d after %.1f s"), static_cast<int32>(phase), mTotalTimeS);
    switch (phase)
    {
    case ESOLPlayerThrusterPhase::Settle:
        if (mShips->GetFlightMode() != ESOLFlightMode::Manual)
        {
            pawn.HandleToggleAssist();
        }
        pawn.HandleFreeLook(true);
        pawn.HandleMouseDelta(FVector2d(PTSMOKE_SWING_YAW_PX, PTSMOKE_SWING_PITCH_PX));
        break;
    case ESOLPlayerThrusterPhase::Boost:
        pawn.HandleToggleBoost();
        break;
    case ESOLPlayerThrusterPhase::LowQuality:
        pawn.HandleToggleBoost();                           // Boost off again: 3 G
        pawn.HandleFreeLook(false);                         // The chase camera sees the aft faces the light reaches
        pawn.SetPlumeLightOverride(PTSMOKE_LOW_PRESET, false);
        break;
    case ESOLPlayerThrusterPhase::VrLow:
        pawn.SetPlumeLightOverride(PTSMOKE_LOW_PRESET, true);
        break;
    case ESOLPlayerThrusterPhase::CvarOff:
        pawn.SetPlumeLightOverride(INDEX_NONE, false);
        PlayerThrusterSmokeSetCvar(SOL::PLAYER_PLUME_CVAR, 0);
        break;
    case ESOLPlayerThrusterPhase::WarPower:
    case ESOLPlayerThrusterPhase::Profile:
        if (phase == ESOLPlayerThrusterPhase::Profile)
        {
            PlayerThrusterSmokeApplyCommandLineView(pawn.GetWorld());   // The window exists by now
        }
        if (phase == ESOLPlayerThrusterPhase::WarPower)
        {
            pawn.HandleFreeLook(true);
            pawn.HandleMouseDelta(FVector2d(PTSMOKE_SWING_YAW_PX, PTSMOKE_SWING_PITCH_PX));
        }
        if (phase == ESOLPlayerThrusterPhase::Profile || !mShips->IsAutopilotWarPower())
        {
            const bool bEngaged = mShips->EngageAutopilot(mMarsIndex, true);
            UE_LOG(LogSOL, Log, TEXT("SmokePlayerThruster: War power order to Mars %s"),
                bEngaged ? TEXT("engaged") : TEXT("refused"));
        }
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a state's checks (Q1-Q3 and its own band) and its screenshot
void FSOLPlayerThrusterSmoke::ExitPhase(ASOLShipPawn& pawn, const ESOLPlayerThrusterPhase phase)
{
    UWorld* world = pawn.GetWorld();
    const double c3 = SOLThruster::PlumeColourScalar(3.0, false);
    switch (phase)
    {
    case ESOLPlayerThrusterPhase::Idle:
        CheckPlume(pawn, TEXT("idle"), 0.0, SOL::Thruster::PLUME_MIN_G, 0.0, 0.0, false, false);
        TakeShot(world, TEXT("idle"));
        break;
    case ESOLPlayerThrusterPhase::Normal:
        CheckPlume(pawn, TEXT("normal"), 0.8, 1.2, 0.0, 0.0, true, false);
        TakeShot(world, TEXT("normal"));
        break;
    case ESOLPlayerThrusterPhase::Full:
        CheckPlume(pawn, TEXT("full"), 2.9, 3.1, c3 - 0.05, c3 + 0.05, true, false);
        TakeShot(world, TEXT("full"));
        break;
    case ESOLPlayerThrusterPhase::Boost:
        CheckPlume(pawn, TEXT("boost"), SOL::Thruster::G_REF, 9.1, 1.0, 1.0, true, false);
        TakeShot(world, TEXT("boost"));
        break;
    case ESOLPlayerThrusterPhase::LowQuality:
        CheckPlume(pawn, TEXT("low-quality"), 2.9, 3.1, c3 - 0.05, c3 + 0.05, false, false);
        TakeShot(world, TEXT("chase-lightoff"));
        break;
    case ESOLPlayerThrusterPhase::VrLow:
        CheckPlume(pawn, TEXT("vr-low"), 2.9, 3.1, c3 - 0.05, c3 + 0.05, true, false);
        TakeShot(world, TEXT("chase-lighton"));
        break;
    case ESOLPlayerThrusterPhase::CvarOff:
    {
        const UStaticMeshComponent* mesh = pawn.GetThrusterMesh();
        const UPointLightComponent* light = pawn.GetPlumeLight();
        Check(TEXT("Q4-cvar-off"), mesh != nullptr && light != nullptr && !mesh->IsVisible() && !light->IsVisible(),
            FString::Printf(TEXT("sol.PlayerPlume 0: mesh %s, light %s"), mesh != nullptr && mesh->IsVisible()
            ? TEXT("visible") : TEXT("hidden"), light != nullptr && light->IsVisible() ? TEXT("visible")
            : TEXT("hidden")));
        PlayerThrusterSmokeSetCvar(SOL::PLAYER_PLUME_CVAR, 1);
        break;
    }
    case ESOLPlayerThrusterPhase::WarPower:
        CheckPlume(pawn, TEXT("war-pulse"), PTSMOKE_WAR_PULSE_G, 9.1, 1.0, 1.0, true, true);
        TakeShot(world, TEXT("warpower"));
        break;
    case ESOLPlayerThrusterPhase::WarRecover:
        CheckPlume(pawn, TEXT("war-recover"), PTSMOKE_WAR_RECOVER_LOW_G, PTSMOKE_WAR_RECOVER_HIGH_G, 1.0, 1.0, true,
            true);
        TakeShot(world, TEXT("warrecover"));
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Checks Q1 (pushed floats equal the pawn's plume), Q2 (the plume against SOLThruster of the step's forward G), Q3
// (the hull light) and the state's own band of forward G and colour and its light expectation
void FSOLPlayerThrusterSmoke::CheckPlume(ASOLShipPawn& pawn, const TCHAR* state, const double minG, const double maxG,
    const double minC, const double maxC, const bool bExpectLight, const bool bWarPower)
{
    const FSOLPlayerPlume& plume = pawn.GetPlayerPlume();
    const FVector3f thrustG = pawn.GetPlumeThrustG();
    const UStaticMeshComponent* mesh = pawn.GetThrusterMesh();
    const UPointLightComponent* light = pawn.GetPlumeLight();
    const bool bWarFlag = mShips->IsAutopilotWarPower();

    // Q1: the floats on the mesh
    bool bFloats = mesh != nullptr && mesh->IsVisible();
    float pushed[3] = { -1.0f, -1.0f, -1.0f };
    if (mesh != nullptr)
    {
        const TArray<float>& data = mesh->GetCustomPrimitiveData().Data;
        for (int32 index = 0; index < 3; ++index)
        {
            pushed[index] = data.IsValidIndex(index) ? data[index] : 0.0f;
        }
        bFloats &= FMath::IsNearlyEqual(pushed[SOL::ThrusterMaterialParams::CUSTOM_COLOUR], plume.Plume.Colour,
            PTSMOKE_FLOAT_TOLERANCE) && FMath::IsNearlyEqual(pushed[SOL::ThrusterMaterialParams::CUSTOM_BRIGHTNESS],
            plume.Plume.Brightness, PTSMOKE_FLOAT_TOLERANCE) && FMath::IsNearlyEqual(
            pushed[SOL::ThrusterMaterialParams::CUSTOM_PLUME_LENGTH_M], plume.Plume.LengthM, PTSMOKE_FLOAT_TOLERANCE);
    }
    Check(*FString::Printf(TEXT("Q1-floats-%s"), state), bFloats, FString::Printf(TEXT("pushed c %.4f b %.4f L %.3f m"
        ", computed c %.4f b %.4f L %.3f m"), pushed[0], pushed[1], pushed[2], plume.Plume.Colour,
        plume.Plume.Brightness, plume.Plume.LengthM));

    // Q2: SOLThruster of the step's forward G, and the state's band
    const double g = thrustG.X;
    const double c = SOLThruster::PlumeColourScalar(g, bWarFlag);
    const double b = SOLThruster::PlumeBrightness(g);
    const double lengthM = SOLThruster::PlumeLengthM(SOL::Thruster::PLAYER_HULL_LENGTH_M, g);
    const bool bMatch = FMath::Abs(plume.Plume.Colour - c) <= PTSMOKE_PLUME_TOLERANCE
        && FMath::Abs(plume.Plume.Brightness - b) <= PTSMOKE_PLUME_TOLERANCE
        && FMath::Abs(plume.Plume.LengthM - lengthM) <= PTSMOKE_LENGTH_TOLERANCE;
    const bool bBand = g >= minG && g <= maxG && plume.Plume.Colour >= minC - PTSMOKE_PLUME_TOLERANCE
        && plume.Plume.Colour <= maxC + PTSMOKE_PLUME_TOLERANCE && bWarFlag == bWarPower;
    Check(*FString::Printf(TEXT("Q2-plume-%s"), state), bMatch && bBand, FString::Printf(TEXT("thrust (%.3f, %.3f, "
        "%.3f) G (band %.2f-%.2f), War power %s: c %.4f (expected %.4f, band %.3f-%.3f), b %.4f (%.4f), L %.3f m "
        "(%.3f m)"), thrustG.X, thrustG.Y, thrustG.Z, minG, maxG, bWarFlag ? TEXT("on") : TEXT("off"),
        plume.Plume.Colour, c, minC, maxC, plume.Plume.Brightness, b, plume.Plume.LengthM, lengthM));

    // Q3: the hull light
    const FLinearColor rim = SOLThruster::PlumeLightColour(c);
    const double intensity = bExpectLight ? b * SOL::Thruster::PLAYER_LIGHT_MAX_INTENSITY_CD : 0.0;
    const bool bLightOn = light != nullptr && light->IsVisible();
    const bool bLight = light != nullptr && bLightOn == (bExpectLight && b > 0.0) && (!bLightOn
        || (FMath::Abs(light->Intensity - intensity) <= PTSMOKE_LIGHT_TOLERANCE
        && light->GetLightColor().Equals(rim, PTSMOKE_COLOUR_TOLERANCE)));
    Check(*FString::Printf(TEXT("Q3-light-%s"), state), bLight, FString::Printf(TEXT("light %s (expected %s), %.3f cd "
        "(expected %.3f), colour (%.3f, %.3f, %.3f) (expected (%.3f, %.3f, %.3f)); G vision blend %.3f"),
        bLightOn ? TEXT("on") : TEXT("off"), bExpectLight && b > 0.0 ? TEXT("on") : TEXT("off"),
        light != nullptr ? light->Intensity : 0.0f, intensity, light != nullptr ? light->GetLightColor().R : 0.0f,
        light != nullptr ? light->GetLightColor().G : 0.0f, light != nullptr ? light->GetLightColor().B : 0.0f, rim.R,
        rim.G, rim.B, pawn.GetGVisionBlendWeight()));
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the interleaved profile (A = sol.PlayerPlume 1, B = 0, ordered A B B A per pair of pairs) after
// the War power order has flown a while: the switch, a settle, then the window means; true once done
bool FSOLPlayerThrusterSmoke::TickProfile(UWorld* world, const float realDeltaSeconds)
{
    constexpr int32 windows = 2 * PTSMOKE_PROFILE_PAIRS;
    if (mPhaseTimeS < PTSMOKE_PROFILE_WAR_S)
    {
        return false;
    }
    if (mProfileWindow >= windows)
    {
        return true;
    }
    const bool bPlume = (mProfileWindow % 4 == 0) || (mProfileWindow % 4 == 3);
    constexpr int32 start = PTSMOKE_PROFILE_SETTLE_FRAMES;
    constexpr int32 end = start + PTSMOKE_PROFILE_FRAMES;
    if (mProfileFrame == 0)
    {
        PlayerThrusterSmokeSetCvar(SOL::PLAYER_PLUME_CVAR, bPlume ? 1 : 0);
        mSumGpuMs = mSumGameMs = mSumFrameMs = mSumThrustG = 0.0;
        mSumFrames = 0;
        mLitFrames = 0;
    }
    if (mProfileFrame > start && mProfileFrame <= end)
    {
        const double g = mShips->GetThrustLocalG().X;
        mSumGpuMs += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
        mSumGameMs += FPlatformTime::ToMilliseconds(GGameThreadTime);
        mSumFrameMs += realDeltaSeconds * 1000.0;
        mSumThrustG += g;
        mLitFrames += SOLThruster::PlumeBrightness(g) > 0.0 ? 1 : 0;
        ++mSumFrames;
    }
    if (mProfileFrame == end)
    {
        FVector2D size(0.0, 0.0);
        if (world->GetGameViewport() != nullptr)
        {
            world->GetGameViewport()->GetViewportSize(size);
        }
        const double frames = FMath::Max(mSumFrames, 1);
        UE_LOG(LogSOL, Log, TEXT("SmokePlayerThruster PROFILE window %d %s: viewport %.0fx%.0f gpuMs=%.4f gameMs=%.4f "
            "frameMs=%.3f meanG=%.2f litFrames=%d frames=%d"), mProfileWindow, bPlume ? TEXT("A") : TEXT("B"), size.X,
            size.Y, mSumGpuMs / frames, mSumGameMs / frames, mSumFrameMs / frames, mSumThrustG / frames, mLitFrames,
            mSumFrames);
        ++mProfileWindow;
        mProfileFrame = 0;
        return mProfileWindow >= windows;
    }
    ++mProfileFrame;
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Logs the SUMMARY line, restores the overrides and takes the closing screenshot (which quits)
void FSOLPlayerThrusterSmoke::Finish(ASOLShipPawn& pawn)
{
    pawn.HandleThrust(FVector3d::ZeroVector);
    pawn.HandleFreeLook(false);
    pawn.SetPlumeLightOverride(INDEX_NONE, false);
    if (mShips.IsValid())
    {
        mShips->DisengageAutopilot();
    }
    PlayerThrusterSmokeSetCvar(SOL::SpaceDust::CVAR_ENABLED, 1);
    PlayerThrusterSmokeSetCvar(SOL::PLAYER_PLUME_CVAR, 1);
    UE_LOG(LogSOL, Log, TEXT("SmokePlayerThruster SUMMARY: complete after %.1f s, %d/%d checks run passed%s"),
        mTotalTimeS, mChecksPassed, mChecksRun, mChecksPassed == mChecksRun && (mChecksRun > 0 || mIsProfile)
        ? TEXT("") : TEXT(" (FAILURES)"));
    if (ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>())
    {
        gameMode->TakeSmokeScreenshot();
    }
}

//////////////////////////////////////////////////////////////////////////
// Requests a screenshot with the HUD named PlayerThruster_<width>x<height>_<name>.png
void FSOLPlayerThrusterSmoke::TakeShot(UWorld* world, const TCHAR* name)
{
    FVector2D size(0.0, 0.0);
    if (world->GetGameViewport() != nullptr)
    {
        world->GetGameViewport()->GetViewportSize(size);
    }
    const FString path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / FString::Printf(
        TEXT("PlayerThruster_%dx%d_%s.png"), FMath::RoundToInt32(size.X), FMath::RoundToInt32(size.Y), name));
    FScreenshotRequest::RequestScreenshot(path, true, false);
    UE_LOG(LogSOL, Log, TEXT("SmokePlayerThruster: screenshot %s"), *path);
    mShotHoldS = PTSMOKE_SHOT_HOLD_S;
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLPlayerThrusterSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SmokePlayerThruster CHECK %-22s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"),
        *detail);
}
