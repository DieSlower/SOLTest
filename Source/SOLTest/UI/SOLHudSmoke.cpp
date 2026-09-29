/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHudSmoke.h"

#include "Game/SOLGameMode.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "UI/SOLFlightHud.h"
#include "UI/SOLHudFormat.h"
#include "UI/SOLRadarLayout.h"
#include "UI/SOLSpeedPanelWidget.h"
#include "Universe/SOLBodyRegistrySubsystem.h"

#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"

namespace
{
    // Phase durations (real seconds)
    constexpr double HUDSMOKE_TAP_PHASE_S = 0.4;            // Long enough for a tapped key to take effect
    constexpr double HUDSMOKE_SETUP_S = 1.0;
    constexpr double HUDSMOKE_HOLD_S = 1.5;                 // W held while the panel is open
    constexpr double HUDSMOKE_THRUST_S = 2.0;               // W held after the panel closed
    constexpr double HUDSMOKE_SHOT_SETUP_S = 2.0;
    constexpr double HUDSMOKE_SHOT_WAIT_S = 1.0;
    constexpr double HUDSMOKE_QUIT_WAIT_S = 6.0;            // The game mode quits 3 s after the last screenshot
    constexpr int32 HUDSMOKE_MIN_PHASE_FRAMES = 5;

    // Scripted inputs
    constexpr int32 HUDSMOKE_DIGIT_UP_STEPS = 3;
    constexpr int32 HUDSMOKE_DIGIT_DOWN_STEPS = 2;
    constexpr double HUDSMOKE_SHOT_CAP_MPS = 500.0;         // Cap while flying for the HUD screenshot
    constexpr float HUDSMOKE_SHOT_STICK_X_PX = 40.0f;       // Stick offset shown in the HUD screenshot
    constexpr float HUDSMOKE_SHOT_STICK_Y_PX = 15.0f;
    constexpr double HUDSMOKE_ORBIT_VIEW_HEIGHT_AU = 3.0;   // Vantage above the Sun for the orbit screenshot
    constexpr int32 HUDSMOKE_RADAR_ZOOM_STEPS = 8;          // '-' presses: enough to reach the 1 km floor from any range

    // Verdict thresholds
    constexpr double HUDSMOKE_CAP_RELATIVE_TOLERANCE = 1.0e-9;
    constexpr double HUDSMOKE_BLOCKED_MAX_GAIN_MPS = 0.5;   // W while the panel is open: no speed gained
    constexpr double HUDSMOKE_THRUST_MIN_GAIN_MPS = 100.0;  // W after closing: clearly accelerating
}

//////////////////////////////////////////////////////////////////////////
// Caches the controller, HUD and subsystems and starts the first phase
void FSOLHudSmoke::Start(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    mController = pawn.GetController<APlayerController>();
    mHud = mController.IsValid() ? mController->GetHUD<ASOLFlightHud>() : nullptr;
    mShips = world->GetSubsystem<USOLShipSubsystem>();
    mTargeting = world->GetSubsystem<USOLTargetingSubsystem>();
    mRegistry = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    mEarthIndex = mRegistry.IsValid() ? mRegistry->GetRegistry().FindByName(FName(SOL::BodyNames::EARTH)) : INDEX_NONE;
    UE_LOG(LogSOL, Log, TEXT("SmokeHud: scripted HUD input starting (controller %s, HUD %s)"),
        mController.IsValid() ? *mController->GetName() : TEXT("none"),
        mHud.IsValid() ? *mHud->GetName() : TEXT("none"));
    mPhase = ESOLHudPhase::Setup;
    EnterPhase(mPhase, pawn);
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script (from the pawn's tick); returns true once it has finished
bool FSOLHudSmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    if (mPhase == ESOLHudPhase::Done)
    {
        return true;
    }
    if (!mController.IsValid() || !mShips.IsValid() || !mTargeting.IsValid() || !mRegistry.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeHud: controller or subsystems missing; aborting"));
        mPhase = ESOLHudPhase::Done;
        return true;
    }
    if (!mHud.IsValid())
    {
        mHud = mController->GetHUD<ASOLFlightHud>();
    }

    // Game keys tapped last frame are released now, so each tap spans exactly one processed frame
    for (const FKey& key : mPendingReleases)
    {
        ReleaseKey(key);
    }
    mPendingReleases.Reset();

    mPhaseTimeS += realDeltaSeconds;
    mTotalTimeS += realDeltaSeconds;
    ++mPhaseFrames;
    if (mPhaseTimeS < GetPhaseDuration(mPhase) || mPhaseFrames < HUDSMOKE_MIN_PHASE_FRAMES)
    {
        return false;
    }

    // Phase over: checks, then the next phase (the radar zoom phase repeats once per '-' press)
    ExitPhase(mPhase, pawn);
    const bool bRepeatPhase = mPhase == ESOLHudPhase::RadarZoomIn && mRadarZoomTaps < HUDSMOKE_RADAR_ZOOM_STEPS;
    if (!bRepeatPhase)
    {
        mPhase = static_cast<ESOLHudPhase>(static_cast<uint8>(mPhase) + 1);
    }
    mPhaseTimeS = 0.0;
    mPhaseFrames = 0;
    if (mPhase == ESOLHudPhase::Done)
    {
        return true;
    }
    EnterPhase(mPhase, pawn);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's entry actions (key presses, setup)
void FSOLHudSmoke::EnterPhase(const ESOLHudPhase phase, ASOLShipPawn& pawn)
{
    const FSOLBodyFrameCache& cache = mRegistry->GetUnrealFrameCache();
    const FSOLShipState state = mShips->GetState();
    const FVector3d earthM = cache.PositionsM.IsValidIndex(mEarthIndex) ? cache.PositionsM[mEarthIndex]
        : FVector3d::ZeroVector;
    USOLSpeedPanelWidget* panel = pawn.GetSpeedPanel();
    switch (phase)
    {
    case ESOLHudPhase::Setup:
        SetupShip(state.PositionM, (earthM - state.PositionM).GetSafeNormal(), mShips->GetReferenceVelocityMps());
        break;
    case ESOLHudPhase::SelectEarth:
        TapKey(EKeys::T);
        break;
    case ESOLHudPhase::OrbitsOn:
    case ESOLHudPhase::OrbitsOff:
        TapKey(EKeys::O);
        break;
    case ESOLHudPhase::PredictedPath:
        TapKey(EKeys::P);
        break;
    case ESOLHudPhase::PanelOpen:
    case ESOLHudPhase::EscOpen:
        TapKey(EKeys::F3);
        break;
    case ESOLHudPhase::PanelBlocksThrust:
    case ESOLHudPhase::ThrustAfterClose:
        mReferenceValue = GetRelativeVelocityMps().Size();
        PressKey(EKeys::W);
        break;
    case ESOLHudPhase::DigitUp:
        mReferenceValue = pawn.GetComposedControl().SpeedCapMps;
        for (int32 step = 0; step < HUDSMOKE_DIGIT_UP_STEPS; ++step)
        {
            SlateKey(pawn, EKeys::Up);
        }
        break;
    case ESOLHudPhase::DigitDown:
        mReferenceValue = pawn.GetComposedControl().SpeedCapMps;
        SlateKey(pawn, EKeys::Right);
        for (int32 step = 0; step < HUDSMOKE_DIGIT_DOWN_STEPS; ++step)
        {
            SlateKey(pawn, EKeys::Down);
        }
        break;
    case ESOLHudPhase::UnitCycle:
        mReferenceValue = pawn.GetComposedControl().SpeedCapMps;
        SlateKey(pawn, EKeys::U);
        break;
    case ESOLHudPhase::ListSelect:
    {
        // Tab into the list, walk down from its current row to Earth, Enter
        SlateKey(pawn, EKeys::Tab);
        const int32 start = panel != nullptr ? panel->GetListSelection() : 0;
        for (int32 row = start; row < mEarthIndex; ++row)
        {
            SlateKey(pawn, EKeys::Down);
        }
        for (int32 row = start; row > mEarthIndex; --row)
        {
            SlateKey(pawn, EKeys::Up);
        }
        SlateKey(pawn, EKeys::Enter);
        break;
    }
    case ESOLHudPhase::PanelClose:
        SlateKey(pawn, EKeys::F3);
        break;
    case ESOLHudPhase::EscClose:
        SlateKey(pawn, EKeys::Escape);
        break;
    case ESOLHudPhase::RadarZoomIn:
        if (mRadarZoomTaps == 0)
        {
            mReferenceValue = mHud.IsValid() ? mHud->GetLastRadarRangeM() : 0.0;
        }
        TapKey(EKeys::Hyphen);
        ++mRadarZoomTaps;
        break;
    case ESOLHudPhase::RadarAuto:
        TapKey(EKeys::Home);
        break;
    case ESOLHudPhase::HudShotSetup:
    {
        // Fly forward-right at a modest cap with a small stick offset, so every HUD element has something to show
        SetupShip(state.PositionM, (earthM - state.PositionM).GetSafeNormal(), mShips->GetReferenceVelocityMps());
        pawn.ApplySpeedCapMps(HUDSMOKE_SHOT_CAP_MPS);
        PressKey(EKeys::W);
        PressKey(EKeys::D);
        mController->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseX, IE_Axis, HUDSMOKE_SHOT_STICK_X_PX, 1));
        mController->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseY, IE_Axis, HUDSMOKE_SHOT_STICK_Y_PX, 1));
        break;
    }
    case ESOLHudPhase::OrbitShotSetup:
    {
        // 3 AU above the Sun (Unreal-handed +Z is ecliptic north), looking straight down, at rest in the Sun's frame
        const int32 sunIndex = mRegistry->GetRegistry().FindByName(FName(SOL::BodyNames::SUN));
        const FVector3d sunM = cache.PositionsM.IsValidIndex(sunIndex) ? cache.PositionsM[sunIndex]
            : FVector3d::ZeroVector;
        const FVector3d sunVelocity = cache.VelocitiesMps.IsValidIndex(sunIndex) ? cache.VelocitiesMps[sunIndex]
            : FVector3d::ZeroVector;
        SetupShip(sunM + FVector3d(0.0, 0.0, HUDSMOKE_ORBIT_VIEW_HEIGHT_AU * SOL::AU_M), FVector3d(0.0, 0.0, -1.0),
            sunVelocity);
        TapKey(EKeys::O);
        break;
    }
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's exit checks
void FSOLHudSmoke::ExitPhase(const ESOLHudPhase phase, ASOLShipPawn& pawn)
{
    const double cap = pawn.GetComposedControl().SpeedCapMps;
    const double relSpeed = GetRelativeVelocityMps().Size();
    const USOLSpeedPanelWidget* panel = pawn.GetSpeedPanel();
    const FSOLSpeedStepperState stepper = panel != nullptr ? panel->GetStepperState() : FSOLSpeedStepperState();
    const bool bOrbits = mHud.IsValid() && mHud->AreOrbitLinesVisible();
    const int32 segments = mHud.IsValid() ? mHud->GetLastOrbitSegmentsDrawn() : 0;
    ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>();

    // Each phase checks one expectation
    switch (phase)
    {
    case ESOLHudPhase::SelectEarth:
        Check(TEXT("a-select"), mTargeting->GetSelectedIndex() == mEarthIndex, FString::Printf(TEXT("T facing Earth: "
            "selected index %d (Earth %d); radar %d contacts, range %.3g m"), mTargeting->GetSelectedIndex(),
            mEarthIndex, mHud.IsValid() ? mHud->GetLastRadarContactCount() : 0,
            mHud.IsValid() ? mHud->GetLastRadarRangeM() : 0.0));

        // Target selected with the radar still in AUTO (no zoom keys yet): the label should read the 1 km floor
        if (gameMode != nullptr)
        {
            gameMode->CaptureScreenshot();
        }
        break;
    case ESOLHudPhase::OrbitsOn:
        Check(TEXT("b-orbits-on"), bOrbits && segments > 0, FString::Printf(TEXT("O: orbit lines %s, %d segments "
            "drawn last frame"), bOrbits ? TEXT("ON") : TEXT("OFF"), segments));
        break;
    case ESOLHudPhase::OrbitsOff:
        Check(TEXT("b-orbits-off"), !bOrbits && segments == 0, FString::Printf(TEXT("O again: orbit lines %s, %d "
            "segments drawn last frame"), bOrbits ? TEXT("ON") : TEXT("OFF"), segments));
        break;
    case ESOLHudPhase::PredictedPath:
        Check(TEXT("b-path-noop"), !bOrbits && !pawn.IsSpeedPanelOpen(),
            TEXT("P: reserved no-op, HUD state unchanged"));
        break;
    case ESOLHudPhase::PanelOpen:
    case ESOLHudPhase::EscOpen:
        Check(phase == ESOLHudPhase::PanelOpen ? TEXT("c-panel-open") : TEXT("f-esc-open"), pawn.IsSpeedPanelOpen()
            && panel != nullptr && panel->IsInViewport(), FString::Printf(TEXT("F3: panel %s; stepper %.2f unit %d "
            "digit %d (cap %.3f m/s)"), pawn.IsSpeedPanelOpen() ? TEXT("open, ship input suspended") : TEXT("closed"),
            stepper.ValueInUnit, static_cast<int32>(stepper.Unit), stepper.HighlightedDigit, cap));
        break;
    case ESOLHudPhase::PanelBlocksThrust:
        Check(TEXT("c-panel-blocks-w"), relSpeed <= mReferenceValue + HUDSMOKE_BLOCKED_MAX_GAIN_MPS
            && pawn.GetComposedControl().Thrust.IsNearlyZero(), FString::Printf(TEXT("W held %.2f s with the panel "
            "open: relative speed %.3f -> %.3f m/s, thrust (%.2f, %.2f, %.2f)"), mPhaseTimeS, mReferenceValue,
            relSpeed, pawn.GetComposedControl().Thrust.X, pawn.GetComposedControl().Thrust.Y,
            pawn.GetComposedControl().Thrust.Z));
        ReleaseKey(EKeys::W);
        break;
    case ESOLHudPhase::DigitUp:
    {
        const double expected = SOLHudFormat::FromUnitValue(SOLHudFormat::ToUnitValue(mReferenceValue, stepper.Unit)
            + HUDSMOKE_DIGIT_UP_STEPS * FMath::Pow(10.0, stepper.HighlightedDigit), stepper.Unit);
        Check(TEXT("d-digit-up"), FMath::Abs(cap - expected) <= HUDSMOKE_CAP_RELATIVE_TOLERANCE * expected,
            FString::Printf(TEXT("Up x%d on digit %d: cap %.3f -> %.3f m/s (expected %.3f)"), HUDSMOKE_DIGIT_UP_STEPS,
                stepper.HighlightedDigit, mReferenceValue, cap, expected));
        break;
    }
    case ESOLHudPhase::DigitDown:
    {
        const double expected = SOLHudFormat::FromUnitValue(SOLHudFormat::ToUnitValue(mReferenceValue, stepper.Unit)
            - HUDSMOKE_DIGIT_DOWN_STEPS * FMath::Pow(10.0, -1), stepper.Unit);
        Check(TEXT("d-digit-down"), stepper.HighlightedDigit == -1
            && FMath::Abs(cap - expected) <= HUDSMOKE_CAP_RELATIVE_TOLERANCE * expected, FString::Printf(TEXT("Right, "
            "Down x%d: digit %d, cap %.3f -> %.3f m/s (expected %.3f)"), HUDSMOKE_DIGIT_DOWN_STEPS,
            stepper.HighlightedDigit, mReferenceValue, cap, expected));
        break;
    }
    case ESOLHudPhase::UnitCycle:
        Check(TEXT("d-unit-cycle"), stepper.Unit == ESOLSpeedUnit::LightSpeed
            && FMath::Abs(cap - mReferenceValue) <= HUDSMOKE_CAP_RELATIVE_TOLERANCE * mReferenceValue,
            FString::Printf(TEXT("U: unit %d (km/s -> c expected 2), value %.6f, cap %.3f m/s unchanged from %.3f"),
                static_cast<int32>(stepper.Unit), stepper.ValueInUnit, cap, mReferenceValue));
        break;
    case ESOLHudPhase::ListSelect:
    {
        const double earthSpeed = panel != nullptr ? panel->GetListedBodySpeedMps(mEarthIndex) : 0.0;
        Check(TEXT("e-list-earth"), panel != nullptr && panel->GetListSelection() == mEarthIndex
            && !panel->IsListFocused() && pawn.IsSpeedPanelOpen()
            && FMath::Abs(cap - earthSpeed) <= HUDSMOKE_CAP_RELATIVE_TOLERANCE * earthSpeed,
            FString::Printf(TEXT("Tab, rows to Earth, Enter: row %d, Earth's listed orbital speed %.3f m/s, cap %.3f "
                "m/s, stepper %.2f unit %d"), panel != nullptr ? panel->GetListSelection() : -1, earthSpeed, cap,
                stepper.ValueInUnit, static_cast<int32>(stepper.Unit)));
        break;
    }
    case ESOLHudPhase::PanelClose:
        Check(TEXT("e-f3-close"), !pawn.IsSpeedPanelOpen() && (panel == nullptr || !panel->IsInViewport()),
            FString::Printf(TEXT("F3: panel %s, cap kept at %.3f m/s"), pawn.IsSpeedPanelOpen() ? TEXT("open")
                : TEXT("closed, ship input restored"), cap));
        break;
    case ESOLHudPhase::ThrustAfterClose:
        Check(TEXT("e-w-after-close"), relSpeed >= mReferenceValue + HUDSMOKE_THRUST_MIN_GAIN_MPS,
            FString::Printf(TEXT("W held %.2f s after closing: relative speed %.3f -> %.1f m/s (cap %.1f)"),
                mPhaseTimeS, mReferenceValue, relSpeed, cap));
        ReleaseKey(EKeys::W);
        break;
    case ESOLHudPhase::EscClose:
        Check(TEXT("f-esc-close"), !pawn.IsSpeedPanelOpen(), FString::Printf(TEXT("Esc: panel %s"),
            pawn.IsSpeedPanelOpen() ? TEXT("open") : TEXT("closed")));
        break;
    case ESOLHudPhase::RadarZoomIn:
    {
        // Checked once, after the last press
        if (mRadarZoomTaps < HUDSMOKE_RADAR_ZOOM_STEPS)
        {
            break;
        }
        const double rangeM = mHud.IsValid() ? mHud->GetLastRadarRangeM() : 0.0;
        const bool bManual = mHud.IsValid() && mHud->IsRadarManual();

        // Starting at the floor every press is a no-op and AUTO is kept (Amendment 4); otherwise it zooms to MANUAL
        const double floorM = SOLRadar::EffectiveFloorM(mTargeting->GetSelectedTarget() != nullptr);
        const bool bZoomOk = mReferenceValue <= floorM ? (!bManual && rangeM == floorM)
            : (bManual && rangeM < SOLRadar::DefaultFloorM && rangeM >= SOLRadar::TargetedFloorM);
        Check(TEXT("h-radar-zoom"), bZoomOk,
            FString::Printf(TEXT("'-' x%d with %s selected: radar %s, range %.4g -> %.4g m"), mRadarZoomTaps,
                mTargeting->GetSelectedTarget() != nullptr ? *mTargeting->GetSelectedTarget()->Name.ToString()
                : TEXT("nothing"), bManual ? TEXT("MANUAL") : TEXT("AUTO"), mReferenceValue, rangeM));
        break;
    }
    case ESOLHudPhase::RadarAuto:
    {
        // AUTO range is the floor for the current target state (SDD 2 Appendix C Amendment 3)
        const double rangeM = mHud.IsValid() ? mHud->GetLastRadarRangeM() : 0.0;
        const double floorM = SOLRadar::EffectiveFloorM(mTargeting->GetSelectedTarget() != nullptr);
        Check(TEXT("h-radar-auto"), mHud.IsValid() && !mHud->IsRadarManual() && rangeM == floorM,
            FString::Printf(TEXT("Home: radar %s, range %.4g m (expected floor %.4g m)"), mHud.IsValid()
                && mHud->IsRadarManual() ? TEXT("MANUAL") : TEXT("AUTO"), rangeM, floorM));
        break;
    }
    case ESOLHudPhase::HudShotSetup:
        UE_LOG(LogSOL, Log, TEXT("SmokeHud: HUD screenshot - relative speed %.1f m/s, cap %.1f, frame %s, radar "
            "range %.3g m"), relSpeed, cap, *mTargeting->GetFrameName().ToString(),
            mHud.IsValid() ? mHud->GetLastRadarRangeM() : 0.0);
        if (gameMode != nullptr)
        {
            gameMode->CaptureScreenshot();
        }
        break;
    case ESOLHudPhase::HudShot:
        ReleaseKey(EKeys::W);
        ReleaseKey(EKeys::D);
        TapKey(EKeys::MiddleMouseButton);
        break;
    case ESOLHudPhase::OrbitShotSetup:
        Check(TEXT("g-orbit-view"), bOrbits && segments > 0, FString::Printf(TEXT("3 AU above the Sun: orbit lines "
            "%s, %d segments drawn"), bOrbits ? TEXT("ON") : TEXT("OFF"), segments));
        UE_LOG(LogSOL, Log, TEXT("SmokeHud: complete after %.1f s, %d/%d checks passed%s; %d panel keys not delivered "
            "by Slate"), mTotalTimeS, mChecksPassed, mChecksRun, mChecksPassed == mChecksRun ? TEXT("")
            : TEXT(" (FAILURES)"), mSlateMisses);
        if (gameMode != nullptr)
        {
            gameMode->TakeSmokeScreenshot();
        }
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the minimum real duration of a phase in seconds
double FSOLHudSmoke::GetPhaseDuration(const ESOLHudPhase phase)
{
    switch (phase)
    {
    case ESOLHudPhase::Setup:
        return HUDSMOKE_SETUP_S;
    case ESOLHudPhase::PanelBlocksThrust:
        return HUDSMOKE_HOLD_S;
    case ESOLHudPhase::ThrustAfterClose:
        return HUDSMOKE_THRUST_S;
    case ESOLHudPhase::HudShotSetup:
    case ESOLHudPhase::OrbitShotSetup:
        return HUDSMOKE_SHOT_SETUP_S;
    case ESOLHudPhase::HudShot:
        return HUDSMOKE_SHOT_WAIT_S;
    case ESOLHudPhase::OrbitShot:
        return HUDSMOKE_QUIT_WAIT_S;
    default:
        return HUDSMOKE_TAP_PHASE_S;
    }
}

//////////////////////////////////////////////////////////////////////////
// Sends a key press to the player controller (game input)
void FSOLHudSmoke::PressKey(const FKey& key) const
{
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Pressed, 1.0f));
}

//////////////////////////////////////////////////////////////////////////
// Sends a key release to the player controller (game input)
void FSOLHudSmoke::ReleaseKey(const FKey& key) const
{
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Released, 0.0f));
}

//////////////////////////////////////////////////////////////////////////
// Presses a game key now and releases it on the next frame
void FSOLHudSmoke::TapKey(const FKey& key)
{
    PressKey(key);
    mPendingReleases.Add(key);
}

//////////////////////////////////////////////////////////////////////////
// Sends a key press and release through Slate to the focused widget (the open panel)
void FSOLHudSmoke::SlateKey(ASOLShipPawn& pawn, const FKey& key)
{
    USOLSpeedPanelWidget* panel = pawn.GetSpeedPanel();
    if (panel == nullptr || !FSlateApplication::IsInitialized())
    {
        UE_LOG(LogSOL, Warning, TEXT("SmokeHud: no panel or Slate for key %s"), *key.ToString());
        return;
    }

    // Real keyboard route: Slate hands the event to the widget with user focus
    const int32 before = panel->GetSlateKeyCount();
    FSlateApplication& slate = FSlateApplication::Get();
    const FKeyEvent keyEvent(key, FModifierKeysState(), slate.GetUserIndexForKeyboard(), false, 0, 0);
    slate.ProcessKeyDownEvent(keyEvent);
    slate.ProcessKeyUpEvent(keyEvent);
    if (panel->GetSlateKeyCount() == before)
    {
        ++mSlateMisses;
        UE_LOG(LogSOL, Warning, TEXT("SmokeHud: Slate did not deliver %s to the panel; calling it directly"),
            *key.ToString());
        panel->HandlePanelKey(key);
    }
}

//////////////////////////////////////////////////////////////////////////
// Teleports the ship to a position (Unreal-handed m) with a velocity, facing a direction, not rotating
void FSOLHudSmoke::SetupShip(const FVector3d& positionM, const FVector3d& forwardDir,
    const FVector3d& velocityMps) const
{
    FSOLShipState state = mShips->GetState();
    state.PositionM = positionM;
    state.VelocityMps = velocityMps;
    state.AngularVelocityRadPerS = FVector3d::ZeroVector;
    if (!forwardDir.IsNearlyZero())
    {
        state.Orientation = UE::Math::TRotationMatrix<double>::MakeFromX(forwardDir).ToQuat();
    }
    mShips->SetState(state);
    UE_LOG(LogSOL, Log, TEXT("SmokeHud: setup - ship teleported to (%.4g, %.4g, %.4g) m facing (%.2f, %.2f, %.2f)"),
        positionM.X, positionM.Y, positionM.Z, forwardDir.X, forwardDir.Y, forwardDir.Z);
}

//////////////////////////////////////////////////////////////////////////
// Returns the ship's velocity relative to the active reference frame
FVector3d FSOLHudSmoke::GetRelativeVelocityMps() const
{
    return mShips->GetState().VelocityMps - mShips->GetReferenceVelocityMps();
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLHudSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SmokeHud CHECK %-16s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"), *detail);
}
