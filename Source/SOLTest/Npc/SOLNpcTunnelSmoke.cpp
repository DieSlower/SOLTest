/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcTunnelSmoke.h"

#include "Game/SOLGameMode.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "Npc/SOLNpcLaneMath.h"
#include "Npc/SOLNpcScaleMath.h"
#include "Npc/SOLNpcSubsystem.h"
#include "Npc/SOLNpcTunnelComponent.h"
#include "Npc/SOLNpcTunnelMath.h"
#include "Npc/SOLNpcVisuals.h"
#include "OrbitLines/SOLOrbitLineVisuals.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "UI/SOLFlightHud.h"
#include "UI/SOLHudShapeMath.h"
#include "UI/SOLLaneTunnelLabel.h"
#include "HudEvents/SOLHudEventSubsystem.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLRenderPlacement.h"

#include "Algo/Sort.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
    // Phase durations (real seconds)
    constexpr double TUNNELSMOKE_WAIT_TIMEOUT_S = 240.0;     // Population spawn and upload (1M ships)
    constexpr double TUNNELSMOKE_VIEW_SETUP_S = 2.5;         // Anchor, rebase and the far layer settle after a move
    constexpr double TUNNELSMOKE_SHOT_S = 0.8;               // The screenshot is written before the next change
    constexpr double TUNNELSMOKE_HOLD_S = 2.0;               // T7 rest and each flight
    constexpr double TUNNELSMOKE_APPROACH_S = 3.0;           // T7 move toward the axis
    constexpr double TUNNELSMOKE_STEP_S = 1.0;               // A switch reaches the component and the HUD
    constexpr double TUNNELSMOKE_MAP_S = 3.0;
    constexpr int32 TUNNELSMOKE_MIN_PHASE_FRAMES = 5;

    // Script values
    constexpr int32 TUNNELSMOKE_SHIPS = 1000000;             // Population (SDD 49 section 8: 1M NPCs)
    const TCHAR* const TUNNELSMOKE_VIEW_NAMES[] = { TEXT("T1"), TEXT("T2"), TEXT("T3"), TEXT("T4"), TEXT("T4b"),
        TEXT("T5"), TEXT("T6") };
    constexpr int32 TUNNELSMOKE_FIRST_VIEWS = 6;             // T1 .. T5 with T4b before T7; T6 comes last
    constexpr double TUNNELSMOKE_T1_RADII = 75.0;            // T1: lane radii beside the lane's middle
    constexpr double TUNNELSMOKE_T1_EXPECTED_FADE = 0.415;   // ...TunnelFade there (SDD 49 section 8)
    constexpr double TUNNELSMOKE_T1_LOOK_DEG = 20.0;         // ...looking along the lane, this far toward it
    constexpr double TUNNELSMOKE_QUARTER = 0.25;             // T2-T4b, T6, T7: a quarter of the way from Earth
    constexpr double TUNNELSMOKE_T4B_YAW_DEG = 1.5;          // T4b: looking this little off the axis
    constexpr double TUNNELSMOKE_FLY_SPEED_RADII_S = 1.0;    // T7: flight speed, tunnel radii per second
    constexpr double TUNNELSMOKE_APPROACH_FROM_RADII = 50.0; // T7: the move toward the axis, tunnel radii
    constexpr double TUNNELSMOKE_APPROACH_TO_RADII = 1.0;
    constexpr double TUNNELSMOKE_REST_PHASE_TOLERANCE = 0.006;   // Ring spacings over the rest (lane motion, SDD 4.4)
    constexpr double TUNNELSMOKE_SPEED_TOLERANCE = 0.01;     // Flight: foot arc vs speed x time (1%)...
    constexpr double TUNNELSMOKE_LAG_S = 0.05;               // ...plus the feed's frame of lag
    constexpr int32 TUNNELSMOKE_CAPTURE_FRAME = 3;           // T7: frames after a part starts before its measurement
    constexpr double TUNNELSMOKE_PHASE_TOLERANCE = 1.0e-6;   // The phase follows the foot arc (unwrapped, spacings)
    constexpr double TUNNELSMOKE_FIXED_RING_TOLERANCE_RADII = 1.0e-3;    // A lane-fixed ring's per-frame move, Rt
    constexpr double TUNNELSMOKE_FADE_TOLERANCE = 1.0e-3;    // A fade recomputed a frame later
    constexpr float TUNNELSMOKE_GLOW_TOLERANCE = 1.0e-5f;
    constexpr double TUNNELSMOKE_PROJECT_CM = 1.0e5;         // Points projected along their direction from the camera

    //////////////////////////////////////////////////////////////////////////
    // Sets an integer console variable (sol.NpcTunnel, sol.SpaceDust)
    void TunnelSmokeSetCvar(const TCHAR* name, const int32 value)
    {
        if (IConsoleVariable* cvar = IConsoleManager::Get().FindConsoleVariable(name))
        {
            cvar->Set(value, ECVF_SetByCode);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the wrapped signed difference b - a of two phases in [-0.5, 0.5)
    double TunnelSmokePhaseDelta(const double a, const double b)
    {
        const double d = b - a;
        return d - FMath::FloorToDouble(d + 0.5);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two float texels match exactly
    bool TunnelSmokeTexelEqual(const FVector4f& a, const FVector4f& b)
    {
        return a.X == b.X && a.Y == b.Y && a.Z == b.Z && a.W == b.W;
    }
}

//////////////////////////////////////////////////////////////////////////
// Caches the subsystems and actors, sets up the overrides and the population, and starts the first phase
void FSOLNpcTunnelSmoke::Start(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    const ASOLGameMode* gameMode = world->GetAuthGameMode<ASOLGameMode>();
    mVisuals = gameMode != nullptr ? gameMode->GetNpcVisuals() : nullptr;
    mOrbitLines = gameMode != nullptr ? gameMode->GetOrbitLineVisuals() : nullptr;
    mNpcs = world->GetSubsystem<USOLNpcSubsystem>();
    mShips = world->GetSubsystem<USOLShipSubsystem>();
    mRegistry = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    mAnchor = world->GetSubsystem<USOLAnchorSubsystem>();
    mMenu = world->GetSubsystem<USOLMenuSubsystem>();
    mSettings = USOLSettingsSubsystem::Get(&pawn);
    mController = pawn.GetController<APlayerController>();
    mHud = mController.IsValid() ? Cast<ASOLFlightHud>(mController->GetHUD()) : nullptr;
    UE_LOG(LogSOL, Log, TEXT("SmokeNpcTunnel: starting (NPC visuals %s, HUD %s, settings %s)"),
        mVisuals.IsValid() ? *mVisuals->GetName() : TEXT("none"), mHud.IsValid() ? *mHud->GetName() : TEXT("none"),
        mSettings.IsValid() ? TEXT("yes") : TEXT("none"));

    // Overrides: the lanes (and so the tunnels) and the orbit lines leave the player's settings alone; orbit lines off
    if (mVisuals.IsValid())
    {
        mVisuals->SetLaneVerificationOverride(true);
    }
    if (mOrbitLines.IsValid())
    {
        mOrbitLines->BeginVerificationOverride();
        mOrbitLines->SetLinesEnabled(false);
    }
    TunnelSmokeSetCvar(SOL::SpaceDust::CVAR_ENABLED, 0);
    if (mNpcs.IsValid() && mNpcs->GetRequestedCount() == 0)
    {
        mNpcs->RequestNpcCount(TUNNELSMOKE_SHIPS);
    }
    if (mShips.IsValid())
    {
        mStartPositionM = SOLRender::EclipticToUnreal(mShips->GetState().PositionM);
    }
    mPhase = ESOLTunnelPhase::WaitPopulation;
    EnterPhase(mPhase, pawn);
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script; returns true once it has finished
bool FSOLNpcTunnelSmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    if (mPhase == ESOLTunnelPhase::Done)
    {
        return true;
    }
    if (!mVisuals.IsValid() || !mNpcs.IsValid() || !mShips.IsValid() || !mRegistry.IsValid() || !mAnchor.IsValid()
        || mVisuals->GetTunnels() == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeNpcTunnel: NPC visuals or subsystems missing; aborting"));
        UE_LOG(LogSOL, Log, TEXT("SmokeNpcTunnel SUMMARY: aborted, %d/%d checks passed (FAILURES)"), mChecksPassed,
            mChecksRun);
        mPhase = ESOLTunnelPhase::Done;
        if (ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>())
        {
            gameMode->TakeSmokeScreenshot();
        }
        return true;
    }
    mPhaseTimeS += realDeltaSeconds;
    mTotalTimeS += realDeltaSeconds;
    ++mPhaseFrames;
    TickPhase(mPhase);

    // The population waits end as soon as a tunnel draws, or the new empty population is uploaded (or time out)
    if (mPhase == ESOLTunnelPhase::WaitPopulation || mPhase == ESOLTunnelPhase::Repopulate)
    {
        const bool bReady = mPhase == ESOLTunnelPhase::WaitPopulation
            ? mNpcs->GetRails().Num() >= TUNNELSMOKE_SHIPS && mVisuals->GetLaneMeshVertexCount() > 0
            : mNpcs->GetPopulationRevision() != mPopulationRevision && mNpcs->GetRails().Num() > 0
                && mPhaseTimeS > TUNNELSMOKE_STEP_S;
        if (!bReady && mPhaseTimeS < TUNNELSMOKE_WAIT_TIMEOUT_S)
        {
            return false;
        }
    }
    else if (mPhaseTimeS < GetPhaseDuration(mPhase) || mPhaseFrames < TUNNELSMOKE_MIN_PHASE_FRAMES)
    {
        return false;
    }

    ExitPhase(mPhase, pawn);
    mPhase = NextPhase(mPhase);
    mPhaseTimeS = 0.0;
    mPhaseFrames = 0;
    if (mPhase == ESOLTunnelPhase::Done)
    {
        return true;
    }
    EnterPhase(mPhase, pawn);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Returns the next phase (views T1 .. T5 in turn, then the single steps)
FSOLNpcTunnelSmoke::ESOLTunnelPhase FSOLNpcTunnelSmoke::NextPhase(const ESOLTunnelPhase phase)
{
    if (phase == ESOLTunnelPhase::ViewCheck && mView + 1 < TUNNELSMOKE_FIRST_VIEWS)
    {
        ++mView;
        return ESOLTunnelPhase::ViewSetup;
    }
    if (phase == ESOLTunnelPhase::OverrideHidden)
    {
        return ESOLTunnelPhase::Done;
    }
    return static_cast<ESOLTunnelPhase>(static_cast<uint8>(phase) + 1);
}

//////////////////////////////////////////////////////////////////////////
// Returns the minimum real duration of a phase
double FSOLNpcTunnelSmoke::GetPhaseDuration(const ESOLTunnelPhase phase)
{
    switch (phase)
    {
    case ESOLTunnelPhase::ViewSetup:
    case ESOLTunnelPhase::EmptySetup:
        return TUNNELSMOKE_VIEW_SETUP_S;
    case ESOLTunnelPhase::ViewCheck:
    case ESOLTunnelPhase::EmptyCheck:
        return TUNNELSMOKE_SHOT_S;
    case ESOLTunnelPhase::RestHold:
    case ESOLTunnelPhase::FlyForward:
    case ESOLTunnelPhase::FlyBack:
        return TUNNELSMOKE_HOLD_S;
    case ESOLTunnelPhase::Approach:
        return TUNNELSMOKE_APPROACH_S;
    case ESOLTunnelPhase::MapOpen:
        return TUNNELSMOKE_MAP_S;
    default:
        return TUNNELSMOKE_STEP_S;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's entry actions
void FSOLNpcTunnelSmoke::EnterPhase(const ESOLTunnelPhase phase, ASOLShipPawn& pawn)
{
    switch (phase)
    {
    case ESOLTunnelPhase::ViewSetup:
        SetPendingGlow(SOL::Settings::HUD_GLOW_DEFAULT);
        SetupView();
        break;
    case ESOLTunnelPhase::ViewCheck:
    case ESOLTunnelPhase::EmptyCheck:
        CheckFeed(*FString::Printf(TEXT("%s-feed"), GetViewName()));
        CheckLabels(*FString::Printf(TEXT("%s-labels"), GetViewName()));
        TakeShot(GetViewName());
        break;
    case ESOLTunnelPhase::RestHold:
    case ESOLTunnelPhase::FlyForward:
    case ESOLTunnelPhase::FlyBack:
    case ESOLTunnelPhase::Approach:
    {
        // T7 at the T2 point; the phase and foot arc at the start of each part
        mView = 1;
        mLaneParam = TUNNELSMOKE_QUARTER;
        mUnwrappedPhase = 0.0;
        mApproachMaxStepM = 0.0;
        TickPhase(phase);
        break;
    }
    case ESOLTunnelPhase::GlowSet:
        SetPendingGlow(SOL::Settings::HUD_GLOW_MAX);
        break;
    case ESOLTunnelPhase::GlowRevert:
        if (mSettings.IsValid())
        {
            mSettings->RevertPending();
        }
        break;
    case ESOLTunnelPhase::PauseOpen:
        if (mMenu.IsValid())
        {
            mMenu->RequestEscape();
        }
        break;
    case ESOLTunnelPhase::PauseClose:
        if (mMenu.IsValid())
        {
            mMenu->RequestResume();
        }
        break;
    case ESOLTunnelPhase::HudHide:
        if (mSettings.IsValid())
        {
            SOLSettingsModel::FSettingsValues values = mSettings->GetLiveValues();
            values.bHudVisible = false;
            mSettings->SetPendingValues(values);
        }
        break;
    case ESOLTunnelPhase::HudShow:
        if (mSettings.IsValid())
        {
            mSettings->RevertPending();
        }
        break;
    case ESOLTunnelPhase::LanesOff:
        mVisuals->SetLaneVerificationOverride(false);
        break;
    case ESOLTunnelPhase::LanesOn:
        mVisuals->SetLaneVerificationOverride(true);
        break;
    case ESOLTunnelPhase::CvarOff:
        TunnelSmokeSetCvar(SOL::NPC_TUNNEL_CVAR, 0);
        break;
    case ESOLTunnelPhase::CvarOn:
        TunnelSmokeSetCvar(SOL::NPC_TUNNEL_CVAR, 1);
        break;
    case ESOLTunnelPhase::MapOpen:
        pawn.HandleOpenMap(false);
        break;
    case ESOLTunnelPhase::MapClose:
        pawn.HandleCloseMap();
        break;
    case ESOLTunnelPhase::Repopulate:
        mPopulationRevision = mNpcs->GetPopulationRevision();
        mNpcs->SetOptionValue(SOLNpcScale::EDemoOption::LaneFraction, 0.0);
        break;
    case ESOLTunnelPhase::EmptySetup:
        mView = 6;
        SetupView();
        mOccluderWritesAtStart = mOrbitLines.IsValid() ? mOrbitLines->GetLaneOnlyOccluderWrites() : 0;
        break;
    case ESOLTunnelPhase::OverrideHidden:
        mVisuals->SetTunnelVerificationOverride(false, false);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's per-frame actions: the T7 ship held, flown or moved relative to the Earth-Mars lane
void FSOLNpcTunnelSmoke::TickPhase(const ESOLTunnelPhase phase)
{
    if (phase != ESOLTunnelPhase::RestHold && phase != ESOLTunnelPhase::FlyForward && phase != ESOLTunnelPhase::FlyBack
        && phase != ESOLTunnelPhase::Approach)
    {
        return;
    }
    int32 lane = INDEX_NONE;
    FSOLNpcTunnelBasis basis;
    double lengthM = 0.0;
    double rt = 0.0;
    if (!FindLane(lane, basis, lengthM, rt))
    {
        return;
    }
    const FSOLNpcLaneEntry& entry = mVisuals->GetLaneFrame().Lanes[lane];

    // The lane point: fixed (rest), moving along the axis at a fixed speed (flights), or the move toward the axis
    double axialM = 0.0;
    double radialM = 0.0;
    if (phase == ESOLTunnelPhase::FlyForward || phase == ESOLTunnelPhase::FlyBack)
    {
        const double sign = phase == ESOLTunnelPhase::FlyForward ? 1.0 : -1.0;
        axialM = sign * TUNNELSMOKE_FLY_SPEED_RADII_S * rt * mPhaseTimeS;
    }
    else if (phase == ESOLTunnelPhase::Approach)
    {
        const double t = FMath::Clamp(mPhaseTimeS / TUNNELSMOKE_APPROACH_S, 0.0, 1.0);
        radialM = FMath::Lerp(TUNNELSMOKE_APPROACH_FROM_RADII, TUNNELSMOKE_APPROACH_TO_RADII, t) * rt;
    }
    SetupShip(entry.A + basis.Axis * (mLaneParam * lengthM + axialM) + basis.Up * radialM, basis.Axis);
    mLastSetAxialM = axialM;

    // The measurement starts a few frames into the part (after the jump to its start point reached the feed)
    double phaseNow = 0.0;
    double footArcM = 0.0;
    double footRadialM = 0.0;
    if (mPhaseFrames == TUNNELSMOKE_CAPTURE_FRAME)
    {
        ReadLanePhase(mStartPhase, mStartFootArcM, footRadialM);
        mPrevPhase = mStartPhase;
        mUnwrappedPhase = 0.0;
        mCaptureAxialM = axialM;
        mCaptureTimeS = mPhaseTimeS;
    }

    // Per frame: the phase's unwrapped advance, and in the approach the lane-fixed ring's move inside the constant zone
    else if (mPhaseFrames > TUNNELSMOKE_CAPTURE_FRAME && ReadLanePhase(phaseNow, footArcM, footRadialM))
    {
        const double previousPhase = mPrevPhase;
        mUnwrappedPhase += TunnelSmokePhaseDelta(previousPhase, phaseNow);
        mPrevPhase = phaseNow;
        const double s0 = SOL::NPC_TUNNEL_RING_BASE_SPACING_RADII * rt;
        const double d0 = s0 / SOL::NPC_TUNNEL_RING_SPACING_PER_DISTANCE;
        if (phase == ESOLTunnelPhase::Approach && footRadialM * footRadialM <= d0 * d0 - s0 * s0)
        {
            // Ring slot 0 sits at footArc + (1 - phase) S0 there; modulo S0 it must not move
            const double moveM = FMath::Abs(TunnelSmokePhaseDelta(previousPhase, phaseNow)) * s0;
            mApproachMaxStepM = FMath::Max(mApproachMaxStepM, moveM);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's exit checks
void FSOLNpcTunnelSmoke::ExitPhase(const ESOLTunnelPhase phase, ASOLShipPawn& pawn)
{
    const USOLNpcTunnelComponent* tunnels = mVisuals->GetTunnels();
    int32 lane = INDEX_NONE;
    FSOLNpcTunnelBasis basis;
    double lengthM = 0.0;
    double rt = 0.0;
    const bool bLane = FindLane(lane, basis, lengthM, rt);
    switch (phase)
    {
    case ESOLTunnelPhase::WaitPopulation:
        Check(TEXT("a-population"), mNpcs->GetRails().Num() >= TUNNELSMOKE_SHIPS && bLane
            && tunnels->GetSlotMeshVertexCount() == SOLNpcTunnel::TunnelSlotMeshVertexCount(), FString::Printf(
            TEXT("%d rails, Earth-Mars lane %d, slot mesh %d vertices (TunnelSlotMeshVertexCount %d)"),
            mNpcs->GetRails().Num(), lane, tunnels->GetSlotMeshVertexCount(),
            SOLNpcTunnel::TunnelSlotMeshVertexCount()));
        break;
    case ESOLTunnelPhase::ViewCheck:
    {
        if (mView == 0 && bLane)
        {
            // T1: the fade equals TunnelFade of the logged distance, about 0.415 at 75 lane radii
            const FSOLNpcLaneEntry& entry = mVisuals->GetLaneFrame().Lanes[lane];
            const FVector3d viewpointM = mAnchor->GetViewpointM();
            const double distanceM = SOLNpcTunnel::LaneSegmentDistanceM(entry.A - viewpointM, entry.B - viewpointM);
            const double expected = SOLNpcTunnel::TunnelFade(distanceM, entry.RadiusM);
            const double fade = SOLNpcTunnel::ReadTunnelFadeTexel(tunnels->GetLastTexels(), lane);
            Check(TEXT("T1-fade"), FMath::Abs(fade - expected) <= TUNNELSMOKE_FADE_TOLERANCE
                && FMath::Abs(fade - TUNNELSMOKE_T1_EXPECTED_FADE) <= 0.01, FString::Printf(TEXT("fade %.4f, "
                "TunnelFade(%.4g m = %.2f R) %.4f"), fade, distanceM, distanceM / entry.RadiusM, expected));
        }
        if (mView == 1 || mView == 2)
        {
            CheckSides(mView == 1 ? TEXT("T2-sides") : TEXT("T3-sides"), mView == 1);
        }
        break;
    }
    case ESOLTunnelPhase::RestHold:
    case ESOLTunnelPhase::FlyForward:
    case ESOLTunnelPhase::FlyBack:
    {
        double phaseNow = 0.0;
        double footArcM = 0.0;
        double radialM = 0.0;
        const bool bRead = ReadLanePhase(phaseNow, footArcM, radialM);
        const double spacingM = SOLNpcTunnel::RingSpacingM(0.0, radialM, rt);
        const double arcMoveM = footArcM - mStartFootArcM;
        if (phase == ESOLTunnelPhase::RestHold)
        {
            Check(TEXT("T7-rest"), bRead && FMath::Abs(mUnwrappedPhase) <= TUNNELSMOKE_REST_PHASE_TOLERANCE,
                FString::Printf(TEXT("phase moved %.6f spacings over %.1f s (foot arc %.1f m, lane motion only)"),
                mUnwrappedPhase, mPhaseTimeS - mCaptureTimeS, arcMoveM));
            break;
        }
        const double expectedM = mLastSetAxialM - mCaptureAxialM;
        const bool bSpeed = FMath::Abs(arcMoveM - expectedM) <= TUNNELSMOKE_SPEED_TOLERANCE * FMath::Abs(expectedM)
            + TUNNELSMOKE_FLY_SPEED_RADII_S * rt * TUNNELSMOKE_LAG_S && FMath::Abs(expectedM) > 0.0;
        const bool bPhase = FMath::Abs(mUnwrappedPhase - arcMoveM / spacingM) <= TUNNELSMOKE_PHASE_TOLERANCE
            + 1.0e-6 * FMath::Abs(arcMoveM / spacingM);
        Check(phase == ESOLTunnelPhase::FlyForward ? TEXT("T7-fly-forward") : TEXT("T7-fly-back"),
            bRead && bSpeed && bPhase, FString::Printf(TEXT("foot arc moved %.1f m (speed x time %.1f m), phase "
            "advanced %.6f spacings (arc / spacing %.6f)"), arcMoveM, expectedM, mUnwrappedPhase, arcMoveM / spacingM));
        break;
    }
    case ESOLTunnelPhase::Approach:
        Check(TEXT("T7-approach"), mApproachMaxStepM <= TUNNELSMOKE_FIXED_RING_TOLERANCE_RADII * rt, FString::Printf(
            TEXT("lane-fixed ring moved at most %.3f m per frame inside the constant zone (Rt %.4g m)"),
            mApproachMaxStepM, rt));
        break;
    case ESOLTunnelPhase::GlowSet:
    case ESOLTunnelPhase::GlowRevert:
    {
        const float hudGlow = phase == ESOLTunnelPhase::GlowSet ? SOL::Settings::HUD_GLOW_MAX
            : (mSettings.IsValid() ? mSettings->GetSavedValues().HudGlow : SOL::Settings::HUD_GLOW_DEFAULT);
        const float expected = SOLHudShapeMath::HudGlowScale(mSettings.IsValid() ? hudGlow
            : SOL::Settings::HUD_GLOW_DEFAULT);
        float glow = -1.0f;
        const bool bGlow = tunnels->GetPushedGlowScale(glow);
        Check(phase == ESOLTunnelPhase::GlowSet ? TEXT("glow-200") : TEXT("glow-revert"),
            bGlow && FMath::Abs(glow - expected) <= TUNNELSMOKE_GLOW_TOLERANCE, FString::Printf(TEXT("tunnel glow "
            "scale %.4f (expected %.4f)"), glow, expected));
        break;
    }
    case ESOLTunnelPhase::PauseOpen:
        Check(TEXT("labels-pause"), mHud.IsValid() && mHud->GetLastLaneLabelCount() == 0 && mMenu.IsValid()
            && mMenu->IsMenuShowing(), FString::Printf(TEXT("menu %s, %d labels drawn (geometry %s)"),
            mMenu.IsValid() && mMenu->IsMenuShowing() ? TEXT("showing") : TEXT("hidden"),
            mHud.IsValid() ? mHud->GetLastLaneLabelCount() : -1, tunnels->AreTunnelsDrawn() ? TEXT("drawn")
            : TEXT("hidden")));
        break;
    case ESOLTunnelPhase::HudHide:
        Check(TEXT("labels-hud-hidden"), mHud.IsValid() && mHud->GetLastLaneLabelCount() == 0 && mSettings.IsValid()
            && !mSettings->GetLiveValues().bHudVisible, FString::Printf(TEXT("HUD %s, %d labels drawn (geometry %s)"),
            mSettings.IsValid() && !mSettings->GetLiveValues().bHudVisible ? TEXT("hidden") : TEXT("shown"),
            mHud.IsValid() ? mHud->GetLastLaneLabelCount() : -1, tunnels->AreTunnelsDrawn() ? TEXT("drawn")
            : TEXT("hidden")));
        break;
    case ESOLTunnelPhase::LanesOff:
        CheckHidden(TEXT("lanes-off"));
        break;
    case ESOLTunnelPhase::CvarOff:
        CheckHidden(TEXT("cvar-off"));
        break;
    case ESOLTunnelPhase::LanesOn:
    case ESOLTunnelPhase::CvarOn:
    case ESOLTunnelPhase::MapClose:
        Check(phase == ESOLTunnelPhase::LanesOn ? TEXT("lanes-on") : phase == ESOLTunnelPhase::CvarOn
            ? TEXT("cvar-on") : TEXT("map-close"), tunnels->AreTunnelsDrawn() && tunnels->IsInstanceComponentVisible(),
            FString::Printf(TEXT("tunnels %s, %d instances"), tunnels->AreTunnelsDrawn() ? TEXT("drawn")
            : TEXT("hidden"), tunnels->GetInstanceCount()));
        break;
    case ESOLTunnelPhase::MapOpen:
        CheckHidden(TEXT("map-open"));
        TakeShot(TEXT("map"));
        break;
    case ESOLTunnelPhase::Repopulate:
    {
        int32 ships = 0;
        for (const FSOLNpcLaneEntry& entry : mVisuals->GetLaneFrame().Lanes)
        {
            ships += entry.ShipCount;
        }
        Check(TEXT("T6-repopulate"), mNpcs->GetPopulationRevision() != mPopulationRevision && ships == 0,
            FString::Printf(TEXT("revision %u -> %u, %d lane ships"), mPopulationRevision,
            mNpcs->GetPopulationRevision(), ships));
        break;
    }
    case ESOLTunnelPhase::EmptyCheck:
    {
        // T6: every shown tunnel at the minimum radius, no lane line, the shared occluders still written
        bool bMinimum = tunnels->GetShownLanes().Num() > 0;
        for (const FSOLNpcTunnelLaneData& data : tunnels->GetShownLanes())
        {
            const double radiusM = mVisuals->GetLaneFrame().Lanes[data.Lane].RadiusM;
            bMinimum &= FMath::IsNearlyEqual(data.TunnelRadiusM, radiusM * SOL::NPC_TUNNEL_RADIUS_MIN_SCALE,
                1.0e-6 * radiusM);
        }
        const int32 writes = mOrbitLines.IsValid() ? mOrbitLines->GetLaneOnlyOccluderWrites() : 0;
        Check(TEXT("T6-empty"), bMinimum && !mVisuals->IsLaneComponentVisible() && tunnels->AreTunnelsDrawn()
            && writes > mOccluderWritesAtStart, FString::Printf(TEXT("%d tunnels at the minimum radius %s, lane line "
            "%s, occluder writes %d -> %d"), tunnels->GetShownLanes().Num(), bMinimum ? TEXT("yes") : TEXT("no"),
            mVisuals->IsLaneComponentVisible() ? TEXT("visible") : TEXT("hidden"), mOccluderWritesAtStart, writes));
        break;
    }
    case ESOLTunnelPhase::OverrideHidden:
        CheckHidden(TEXT("override-hidden"));
        Finish(pawn);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Logs the SUMMARY line and takes the closing screenshot (which quits)
void FSOLNpcTunnelSmoke::Finish(ASOLShipPawn& pawn)
{
    TunnelSmokeSetCvar(SOL::SpaceDust::CVAR_ENABLED, 1);
    if (mSettings.IsValid())
    {
        mSettings->RevertPending();
    }
    UE_LOG(LogSOL, Log, TEXT("SmokeNpcTunnel SUMMARY: complete after %.1f s, %d/%d checks run passed%s"),
        mTotalTimeS, mChecksPassed, mChecksRun, mChecksPassed == mChecksRun ? TEXT("") : TEXT(" (FAILURES)"));
    if (ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>())
    {
        gameMode->TakeSmokeScreenshot();
    }
}

//////////////////////////////////////////////////////////////////////////
// Finds the Earth-Mars lane, its basis, length and tunnel radius
bool FSOLNpcTunnelSmoke::FindLane(int32& outLane, FSOLNpcTunnelBasis& outBasis, double& outLengthM,
    double& outTunnelRadiusM) const
{
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    const FSOLNpcGroupTable& groups = mNpcs->GetGroups();
    const FSOLNpcLaneFrame& frame = mVisuals->GetLaneFrame();
    const int32 earth = registry.FindByName(FName(SOL::BodyNames::EARTH));
    const int32 mars = registry.FindByName(FName(SOL::BodyNames::MARS));
    for (int32 lane = 0; lane < frame.Lanes.Num(); ++lane)
    {
        if (groups.LaneBodyA.IsValidIndex(lane) && groups.LaneBodyB.IsValidIndex(lane)
            && groups.LaneBodyA[lane] == earth && groups.LaneBodyB[lane] == mars)
        {
            const FSOLNpcLaneEntry& entry = frame.Lanes[lane];
            outLane = lane;
            outBasis = SOLNpcTunnel::TunnelBasis(entry.A, entry.B);
            outLengthM = (entry.B - entry.A).Size();
            outTunnelRadiusM = SOLNpcTunnel::TunnelRadiusM(entry.RadiusM, entry.ShipCount);
            return outBasis.bValid;
        }
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Places the ship (ecliptic universe metres) facing a direction, at the reference velocity, not rotating
void FSOLNpcTunnelSmoke::SetupShip(const FVector3d& positionM, const FVector3d& forward) const
{
    FSOLShipState state = mShips->GetState();
    state.PositionM = SOLRender::EclipticToUnreal(positionM);
    state.VelocityMps = mShips->GetReferenceVelocityMps();
    state.AngularVelocityRadPerS = FVector3d::ZeroVector;
    if (!forward.IsNearlyZero())
    {
        state.Orientation = UE::Math::TRotationMatrix<double>::MakeFromX(SOLRender::EclipticToUnreal(forward)).ToQuat();
    }
    mShips->SetState(state);
}

//////////////////////////////////////////////////////////////////////////
// Places the ship for the current view (SDD 49 section 8 item 2)
void FSOLNpcTunnelSmoke::SetupView()
{
    int32 lane = INDEX_NONE;
    FSOLNpcTunnelBasis basis;
    double lengthM = 0.0;
    double rt = 0.0;
    FVector3d positionM = mStartPositionM;
    FVector3d forward = FVector3d::UnitX();
    if (FindLane(lane, basis, lengthM, rt))
    {
        const FSOLNpcLaneEntry& entry = mVisuals->GetLaneFrame().Lanes[lane];
        const FVector3d quarterM = entry.A + basis.Axis * (TUNNELSMOKE_QUARTER * lengthM);
        switch (mView)
        {
        case 0:
        {
            // T1: 75 lane radii above the lane's middle, looking along it toward Mars, tilted toward it
            const double tilt = FMath::DegreesToRadians(TUNNELSMOKE_T1_LOOK_DEG);
            positionM = entry.A + basis.Axis * (0.5 * lengthM) + basis.Up * (TUNNELSMOKE_T1_RADII * entry.RadiusM);
            forward = basis.Axis * FMath::Cos(tilt) - basis.Up * FMath::Sin(tilt);
            break;
        }
        case 1:
        case 6:
            positionM = quarterM;
            forward = basis.Axis;
            break;
        case 2:
            positionM = quarterM;
            forward = -basis.Axis;
            break;
        case 3:
            positionM = quarterM;
            forward = basis.Up;
            break;
        case 4:
        {
            const double yaw = FMath::DegreesToRadians(TUNNELSMOKE_T4B_YAW_DEG);
            positionM = quarterM + basis.Right * rt;
            forward = basis.Axis * FMath::Cos(yaw) + basis.Right * FMath::Sin(yaw);
            break;
        }
        default:
        {
            // T5: the start above Earth, facing the Moon (Venus-Earth, Earth-Mars and Earth-Moon meet at Earth)
            const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
            const int32 moon = registry.FindByName(FName(SOL::BodyNames::MOON));
            positionM = mStartPositionM;
            forward = moon != INDEX_NONE ? (registry.GetPositionM(moon) - positionM).GetSafeNormal() : forward;
            break;
        }
        }
    }
    SetupShip(positionM, forward);
    UE_LOG(LogSOL, Log, TEXT("SmokeNpcTunnel: view %s at (%.6g, %.6g, %.6g) m ecliptic facing (%.3f, %.3f, %.3f), "
        "Earth-Mars lane %d, Rt %.4g m"), GetViewName(), positionM.X, positionM.Y, positionM.Z, forward.X, forward.Y,
        forward.Z, lane, rt);
}

//////////////////////////////////////////////////////////////////////////
// Returns the current view's name
const TCHAR* FSOLNpcTunnelSmoke::GetViewName() const
{
    const int32 last = static_cast<int32>(UE_ARRAY_COUNT(TUNNELSMOKE_VIEW_NAMES)) - 1;
    return TUNNELSMOKE_VIEW_NAMES[FMath::Clamp(mView, 0, last)];
}

//////////////////////////////////////////////////////////////////////////
// Checks the instances, the texels against the feed, the fade read back and its crossfade
void FSOLNpcTunnelSmoke::CheckFeed(const TCHAR* label)
{
    const USOLNpcTunnelComponent* tunnels = mVisuals->GetTunnels();
    const TConstArrayView<FSOLNpcTunnelLaneData> shown = tunnels->GetShownLanes();
    const TConstArrayView<int32> instances = tunnels->GetInstanceLanes();
    const TConstArrayView<FVector4f> texels = tunnels->GetLastTexels();

    // Instances = the lanes with a fade above 0 (index order), visible while one draws
    bool bInstances = instances.Num() == shown.Num() && tunnels->GetInstanceCount() == shown.Num()
        && tunnels->IsInstanceComponentVisible() == (shown.Num() > 0);
    for (int32 index = 0; bInstances && index < shown.Num(); ++index)
    {
        bInstances = instances[index] == shown[index].Lane;
    }

    // Every texel equals the feed packed again; each feed entry matches the pure frame recomputed now
    TArray<FVector4f> expected;
    expected.Init(FVector4f(0.0f, 0.0f, 0.0f, 0.0f), SOL::NPC_GROUP_TEXTURE_WIDTH * 4);
    SOLNpcTunnel::WriteTunnelTexels(shown, expected);
    bool bTexels = texels.Num() == expected.Num();
    for (int32 index = 0; bTexels && index < expected.Num(); ++index)
    {
        bTexels = TunnelSmokeTexelEqual(texels[index], expected[index]);
    }
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    const FSOLNpcGroupTable& groups = mNpcs->GetGroups();
    const FVector3d viewpointM = mAnchor->GetViewpointM();
    bool bPure = shown.Num() > 0;
    FString detail;
    for (const FSOLNpcTunnelLaneData& data : shown)
    {
        FSOLNpcTunnelLaneData pure;
        FSOLNpcTunnelFoot foot;
        const bool bTunnel = SOLNpcTunnel::BuildTunnelLaneData(data.Lane, mVisuals->GetLaneFrame().Lanes[data.Lane],
            viewpointM, registry.GetRadiusM(groups.LaneBodyA[data.Lane]),
            registry.GetRadiusM(groups.LaneBodyB[data.Lane]), pure, foot);
        const double footToleranceM = 1.0e-3 * data.TunnelRadiusM + 1.0e-6 * data.FootRelM.Size();
        bPure &= bTunnel && pure.TunnelRadiusM == data.TunnelRadiusM && pure.Axis.Equals(data.Axis, 1.0e-9)
            && pure.Up.Equals(data.Up, 1.0e-9) && FMath::Abs(pure.Fade - data.Fade) <= TUNNELSMOKE_FADE_TOLERANCE
            && pure.FootRelM.Equals(data.FootRelM, footToleranceM);

        // Q5: the fade the lane and point materials read back from the uploaded texels, and the crossfade sum at
        // sampled lane points. The sum uses the C++ mirrors of the lane and point shaders, so it proves the texel
        // round trip and the formulas, not the GPU output (the screenshots cover that)
        const double readBack = SOLNpcTunnel::ReadTunnelFadeTexel(texels, data.Lane);
        const double laneRadiusM = mVisuals->GetLaneFrame().Lanes[data.Lane].RadiusM;
        bPure &= readBack == static_cast<double>(static_cast<float>(data.Fade));
        for (const double radii : { 10.0, 75.0, 150.0, 1000.0 })
        {
            const double d = radii * laneRadiusM;
            bPure &= FMath::IsNearlyEqual(SOLNpcLane::LaneLineFadeWithTunnel(d, laneRadiusM, readBack)
                + SOLNpcLane::LaneShipPointAlpha(d, laneRadiusM, true, readBack), 1.0, 1.0e-9);
        }
        detail += FString::Printf(TEXT(" [lane %d: fade %.4f, Rt %.4g m, h %.4g m, phase %.4f, ends %.4g / %.4g m]"),
            data.Lane, data.Fade, data.TunnelRadiusM, data.FootRelM.Size(), data.Phase, data.EndArcAM, data.EndArcBM);
    }
    Check(label, bInstances && bTexels && bPure, FString::Printf(TEXT("instances %s, texels %s, pure frame %s:%s"),
        bInstances ? TEXT("ok") : TEXT("BAD"), bTexels ? TEXT("ok") : TEXT("BAD"), bPure ? TEXT("ok") : TEXT("BAD"),
        *detail));
}

//////////////////////////////////////////////////////////////////////////
// Checks the label snapshot (strongest first, side alpha) and the drawn boxes (clear of the rows and of each other)
void FSOLNpcTunnelSmoke::CheckLabels(const TCHAR* label)
{
    const USOLHudEventSubsystem* hudEvents = USOLHudEventSubsystem::Get(mVisuals.Get());
    if (hudEvents == nullptr || !mHud.IsValid())
    {
        Check(label, false, TEXT("no HUD or HUD event bus"));
        return;
    }
    const FSOLHudLaneTunnelLabels& snapshot = hudEvents->GetBus().GetLaneTunnelLabels();
    const USOLNpcTunnelComponent* tunnels = mVisuals->GetTunnels();
    const TConstArrayView<FSOLNpcTunnelLaneData> shownLanes = tunnels->GetShownLanes();
    const int32 shown = shownLanes.Num();

    // The expected count: the strongest shown tunnels (capped) whose label alpha is bright enough to place
    double fades[SOL::NPC_GROUP_TEXTURE_WIDTH];
    const int32 fadeCount = FMath::Min(shown, static_cast<int32>(SOL::NPC_GROUP_TEXTURE_WIDTH));
    for (int32 index = 0; index < fadeCount; ++index)
    {
        fades[index] = shownLanes[index].Fade;
    }
    Algo::Sort(TArrayView<double>(fades, fadeCount), TGreater<double>());
    int32 expectedCount = 0;
    for (int32 index = 0; index < FMath::Min(fadeCount, static_cast<int32>(SOL::NPC_TUNNEL_LABEL_MAX_COUNT)); ++index)
    {
        expectedCount += SOLNpcTunnel::IsLabelPlaced(SOLNpcTunnel::LaneLabelAlpha(fades[index], index == 0)) ? 1 : 0;
    }
    bool bSnapshot = snapshot.Count == expectedCount;
    bool bValues = true;
    FString detail = FString::Printf(TEXT(" (expected %d)"), expectedCount);
    for (int32 index = 0; index < snapshot.Count; ++index)
    {
        const FSOLHudLaneTunnelLabel& entry = snapshot.Labels[index];
        bSnapshot &= entry.bStrongest == (index == 0) && FMath::IsNearlyEqual(entry.Alpha,
            SOLNpcTunnel::LaneLabelAlpha(entry.Strength, index == 0), 1.0e-9)
            && (index == 0 || entry.Strength <= snapshot.Labels[index - 1].Strength);

        // The published end distances and anchors equal the pure functions on the same frame's feed (the foot as the
        // lane origin, so the viewpoint is the zero point; the anchors on either ahead side, both on the same one)
        const FSOLNpcTunnelLaneData* data = shownLanes.FindByPredicate([&entry](const FSOLNpcTunnelLaneData& lane)
        {
            return lane.Lane == entry.Lane;
        });
        if (data == nullptr)
        {
            bValues = false;
            continue;
        }
        FSOLNpcTunnelBasis basis;
        basis.Axis = data->Axis;
        basis.Up = data->Up;
        basis.Right = FVector3d::CrossProduct(data->Axis, data->Up);
        basis.bValid = true;
        FSOLNpcLaneEntry footLane = mVisuals->GetLaneFrame().Lanes[entry.Lane];
        footLane.A = data->FootRelM;
        const FVector2D distancesM = SOLNpcTunnel::LaneEndDistancesM(FVector3d::ZeroVector, footLane, basis,
            FVector2D(data->EndArcAM, data->EndArcBM));
        const double distanceToleranceM = 1.0e-6 * (distancesM.X + distancesM.Y) + 1.0;
        bValues &= FMath::Abs(entry.DistanceAM - distancesM.X) <= distanceToleranceM
            && FMath::Abs(entry.DistanceBM - distancesM.Y) <= distanceToleranceM;
        bool bAnchors = false;
        for (const int32 sign : { 1, -1 })
        {
            const FVector3d topM = SOLNpcTunnel::LaneLabelAnchorRelM(data->FootRelM, basis, data->TunnelRadiusM,
                sign, 0.0);
            const FVector3d bottomM = SOLNpcTunnel::LaneLabelAnchorRelM(data->FootRelM, basis, data->TunnelRadiusM,
                sign, 180.0);
            const double anchorToleranceM = 1.0e-9 * topM.Size() + 1.0e-3;
            bAnchors |= entry.TopAnchorM.Equals(topM, anchorToleranceM)
                && entry.BottomAnchorM.Equals(bottomM, anchorToleranceM);
        }
        bValues &= bAnchors;
        detail += FString::Printf(TEXT(" [lane %d: F %.3f, alpha %.3f, %.4g / %.4g m, %d ships, anchor %.4g m]"),
            entry.Lane, entry.Strength, entry.Alpha, entry.DistanceAM, entry.DistanceBM, entry.ShipCount,
            entry.TopAnchorM.Length());
    }

    // The boxes drawn: clear of every projected dot row (a pixel of slack for rounding), of the HUD panels the
    // labels avoid and of each other
    const TConstArrayView<FVector2D> starts = mHud->GetLastLaneRowStarts();
    const TConstArrayView<FVector2D> ends = mHud->GetLastLaneRowEnds();
    bool bBoxes = true;
    for (int32 index = 0; index < mHud->GetLastLaneLabelCount(); ++index)
    {
        const FBox2D& box = mHud->GetLastLaneLabelBox(index);
        bBoxes &= SOLLaneTunnelLabel::LaneLabelClearOfDots(box, starts, ends,
            SOL::NPC_TUNNEL_LABEL_DOT_MARGIN_PX - 1.0);
        for (int32 avoid = 0; avoid < mHud->GetLaneLabelAvoidBoxCount(); ++avoid)
        {
            bBoxes &= !box.Intersect(mHud->GetLaneLabelAvoidBox(avoid));
        }
        for (int32 other = 0; other < index; ++other)
        {
            bBoxes &= !box.Intersect(mHud->GetLastLaneLabelBox(other));
        }
        detail += FString::Printf(TEXT(" {drawn lane %d %s alpha %.2f at (%.0f, %.0f)-(%.0f, %.0f)}"),
            mHud->GetLastLaneLabelLane(index), mHud->IsLastLaneLabelCompact(index) ? TEXT("compact") : TEXT("full"),
            mHud->GetLastLaneLabelAlpha(index), box.Min.X, box.Min.Y, box.Max.X, box.Max.Y);
    }

    // Views from inside or right beside a tube must draw a label, not only publish one (SDD 49 section 8); T4
    // (looking straight at the wall: both anchors off screen) and T5 (the anchors behind Earth) draw none (D19)
    const FString viewName = GetViewName();
    const bool bMustDraw = viewName == TEXT("T2") || viewName == TEXT("T3") || viewName == TEXT("T4b")
        || viewName == TEXT("T6");
    const bool bMustHide = viewName == TEXT("T4") || viewName == TEXT("T5");
    const int32 drawn = mHud->GetLastLaneLabelCount();
    const bool bDrawn = (!bMustDraw || drawn > 0) && (!bMustHide || drawn == 0);
    Check(label, bSnapshot && bValues && bBoxes && bDrawn, FString::Printf(TEXT("%d tunnels shown, %d labels "
        "published, %d drawn%s, %d rows, %d HUD panels avoided, order %s, values %s, boxes %s:%s"), shown,
        snapshot.Count, drawn, bDrawn ? TEXT("") : bMustHide ? TEXT(" (EXPECTED NONE, D19)") : TEXT(" (NONE DRAWN)"),
        starts.Num(), mHud->GetLaneLabelAvoidBoxCount(), bSnapshot ? TEXT("ok") : TEXT("BAD"),
        bValues ? TEXT("pure") : TEXT("BAD"), bBoxes ? TEXT("clear") : TEXT("OVERLAP"), *detail));
}

//////////////////////////////////////////////////////////////////////////
// Checks keep right and the chevron directions on screen at the nearest visible chevron gap ahead. It projects the
// C++ slot geometry (SOLNpcTunnel::ChevronSlot) through the real camera, so it proves the wall sides and directions
// the shader mirrors, not the shader's own output (the T2 / T3 screenshots show that)
void FSOLNpcTunnelSmoke::CheckSides(const TCHAR* label, const bool bLookingTowardB)
{
    int32 lane = INDEX_NONE;
    FSOLNpcTunnelBasis basis;
    double lengthM = 0.0;
    double rt = 0.0;
    const FSOLNpcTunnelLaneData* data = nullptr;
    if (FindLane(lane, basis, lengthM, rt))
    {
        data = mVisuals->GetTunnels()->GetShownLanes().FindByPredicate([lane](const FSOLNpcTunnelLaneData& shown)
        {
            return shown.Lane == lane;
        });
    }
    if (data == nullptr || !mController.IsValid() || mController->PlayerCameraManager == nullptr)
    {
        Check(label, false, TEXT("Earth-Mars tunnel not shown or no camera"));
        return;
    }

    // The nearest visible chevron gap in front (the camera looks along +axis toward B, or -axis toward A)
    FSOLNpcTunnelSlotInput input;
    input.RadialM = data->FootRelM.Size();
    input.TunnelRadiusM = data->TunnelRadiusM;
    input.Phase = data->Phase;
    input.EndArcAM = data->EndArcAM;
    input.EndArcBM = data->EndArcBM;
    double axialM = 0.0;
    bool bFound = false;
    const double lookSign = bLookingTowardB ? 1.0 : -1.0;
    for (int32 slot = 0; slot < SOLNpcTunnel::DetailSlotCount(); ++slot)
    {
        FSOLNpcTunnelRing chevron;
        if (SOLNpcTunnel::ChevronSlot(slot, input, chevron) && chevron.AxialOffsetM * lookSign > 2.0 * rt
            && (!bFound || FMath::Abs(chevron.AxialOffsetM) < FMath::Abs(axialM)))
        {
            axialM = chevron.AxialOffsetM;
            bFound = true;
        }
    }

    // Each wall's chevron centre and tip on screen (directions from the camera, projected a short way out)
    const FVector3d viewpointM = mAnchor->GetViewpointM();
    const FVector cameraCm = mController->PlayerCameraManager->GetCameraLocation();
    FVector2D viewSize(0.0, 0.0);
    if (mVisuals->GetWorld()->GetGameViewport() != nullptr)
    {
        mVisuals->GetWorld()->GetGameViewport()->GetViewportSize(viewSize);
    }
    const FVector2D centerPx = 0.5 * viewSize;
    const auto project = [&](const FVector3d& relM, FVector2D& outScreen)
    {
        const FVector direction = (mAnchor->UniverseToRenderCm(viewpointM + relM) - cameraCm).GetSafeNormal();
        return mController->ProjectWorldLocationToScreen(cameraCm + direction * TUNNELSMOKE_PROJECT_CM, outScreen);
    };
    bool bOk = bFound;
    FString detail = FString::Printf(TEXT("chevron gap at %.3g Rt"), axialM / rt);
    for (int32 wall = 0; wall < 2 && bOk; ++wall)
    {
        // Outbound wall at 3 o'clock pointing toward B, inbound at 9 o'clock pointing toward A
        const bool bOutbound = wall == 0;
        const FVector3d wallDir = bOutbound ? basis.Right : -basis.Right;
        const FVector3d along = bOutbound ? basis.Axis : -basis.Axis;
        const FVector3d centreM = data->FootRelM + basis.Axis * axialM + wallDir
            * (SOL::NPC_TUNNEL_CHEVRON_RADIUS_SCALE * rt);
        const FVector3d tipM = centreM + along * (SOL::NPC_TUNNEL_CHEVRON_HALF_LENGTH_RADII * rt);
        FVector2D centre;
        FVector2D tip;
        bOk &= project(centreM, centre) && project(tipM, tip);

        // Keep right: the outbound wall is on the right while looking toward B; a chevron pointing the way the camera
        // looks has its tip nearer the screen centre (the vanishing point ahead)
        const bool bRight = centre.X > centerPx.X;
        const bool bPointsAhead = FVector2D::Distance(tip, centerPx) < FVector2D::Distance(centre, centerPx);
        const bool bExpectRight = bOutbound == bLookingTowardB;
        const bool bExpectAhead = bOutbound == bLookingTowardB;
        bOk &= bRight == bExpectRight && bPointsAhead == bExpectAhead;
        detail += FString::Printf(TEXT(", %s wall centre (%.0f, %.0f) %s, tip (%.0f, %.0f) %s"),
            bOutbound ? TEXT("outbound") : TEXT("inbound"), centre.X, centre.Y, bRight ? TEXT("right") : TEXT("left"),
            tip.X, tip.Y, bPointsAhead ? TEXT("ahead") : TEXT("back"));
    }
    Check(label, bOk, detail);
}

//////////////////////////////////////////////////////////////////////////
// Checks that the tunnels are hidden and every fade texel is 0 (the #38 crossfade back exactly)
void FSOLNpcTunnelSmoke::CheckHidden(const TCHAR* label)
{
    const USOLNpcTunnelComponent* tunnels = mVisuals->GetTunnels();
    const TConstArrayView<FVector4f> texels = tunnels->GetLastTexels();
    bool bZero = texels.Num() >= 2 * SOL::NPC_GROUP_TEXTURE_WIDTH;
    for (int32 lane = 0; bZero && lane < SOL::NPC_GROUP_TEXTURE_WIDTH; ++lane)
    {
        bZero = SOLNpcTunnel::ReadTunnelFadeTexel(texels, lane) == 0.0;
    }
    Check(label, !tunnels->AreTunnelsDrawn() && !tunnels->IsInstanceComponentVisible() && bZero
        && tunnels->GetShownLanes().Num() == 0, FString::Printf(TEXT("tunnels %s, component %s, %d shown, every fade "
        "texel 0 %s"), tunnels->AreTunnelsDrawn() ? TEXT("drawn") : TEXT("hidden"),
        tunnels->IsInstanceComponentVisible() ? TEXT("visible") : TEXT("hidden"), tunnels->GetShownLanes().Num(),
        bZero ? TEXT("yes") : TEXT("NO")));
}

//////////////////////////////////////////////////////////////////////////
// Returns the Earth-Mars lane's ring phase, foot arc and foot distance from the component's feed
bool FSOLNpcTunnelSmoke::ReadLanePhase(double& outPhase, double& outFootArcM, double& outRadialM) const
{
    int32 lane = INDEX_NONE;
    FSOLNpcTunnelBasis basis;
    double lengthM = 0.0;
    double rt = 0.0;
    if (!FindLane(lane, basis, lengthM, rt))
    {
        return false;
    }
    const FSOLNpcTunnelLaneData* data = mVisuals->GetTunnels()->GetShownLanes().FindByPredicate(
        [lane](const FSOLNpcTunnelLaneData& shown)
        {
            return shown.Lane == lane;
        });
    if (data == nullptr)
    {
        return false;
    }

    // The foot arc from node A: end arc A relative to the foot plus the end arc from A
    const FSOLNpcGroupTable& groups = mNpcs->GetGroups();
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    const FVector2D endArcsM = SOLNpcTunnel::TunnelEndArcsM(lengthM, registry.GetRadiusM(groups.LaneBodyA[lane]),
        registry.GetRadiusM(groups.LaneBodyB[lane]), data->TunnelRadiusM);
    outPhase = data->Phase;
    outFootArcM = endArcsM.X - data->EndArcAM;
    outRadialM = data->FootRelM.Size();
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Sets the pending HUD glow (never applied)
void FSOLNpcTunnelSmoke::SetPendingGlow(const float hudGlow) const
{
    if (mSettings.IsValid())
    {
        SOLSettingsModel::FSettingsValues values = mSettings->GetLiveValues();
        values.HudGlow = hudGlow;
        mSettings->SetPendingValues(values);
    }
}

//////////////////////////////////////////////////////////////////////////
// Requests a screenshot with the HUD named NpcTunnel_<width>x<height>_<name>.png
void FSOLNpcTunnelSmoke::TakeShot(const FString& name) const
{
    FVector2D size(0.0, 0.0);
    if (mVisuals.IsValid() && mVisuals->GetWorld()->GetGameViewport() != nullptr)
    {
        mVisuals->GetWorld()->GetGameViewport()->GetViewportSize(size);
    }
    const FString path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / FString::Printf(
        TEXT("NpcTunnel_%dx%d_%s.png"), FMath::RoundToInt32(size.X), FMath::RoundToInt32(size.Y), *name));
    FScreenshotRequest::RequestScreenshot(path, true, false);
    UE_LOG(LogSOL, Log, TEXT("SmokeNpcTunnel: screenshot %s"), *path);
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLNpcTunnelSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SmokeNpcTunnel CHECK %-18s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"),
        *detail);
}
