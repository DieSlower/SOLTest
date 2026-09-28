/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Ship/SOLShipSmokeFlight.h"

#include "Flight/SOLFlight.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistry.h"
#include "Universe/SOLRenderPlacement.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Engine/World.h"

namespace
{
    // Phase durations (real seconds)
    constexpr double SMOKE_HOVER_S = 15.0;
    constexpr double SMOKE_CRUISE_S = 5.0;
    constexpr double SMOKE_NEWTON_THRUST_S = 5.0;
    constexpr double SMOKE_NEWTON_COAST_S = 5.0;
    constexpr double SMOKE_PITCH_S = 2.0;
    constexpr double SMOKE_BRAKE_S = 10.0;
    constexpr double SMOKE_WARP_S = 5.0;
    constexpr double SMOKE_COLLIDE_S = 14.0;
    constexpr double SMOKE_SURFACE_S = 5.0;
    constexpr double SMOKE_LOG_INTERVAL_S = 1.0;

    // Scripted inputs
    constexpr double SMOKE_CRUISE_CAP_MPS = 100.0;
    constexpr double SMOKE_COLLIDE_START_ALTITUDE_M = 5000.0;
    constexpr int32 SMOKE_WARP_INDEX = 5;                    // 1 day per second, from the warp phase to the end

    // Verdict thresholds
    constexpr double SMOKE_HOVER_MAX_REL_MPS = 1.0;          // Assist has cancelled the spawn orbit speed
    constexpr double SMOKE_CRUISE_MIN_REL_MPS = 90.0;        // 5 s at tau 1.5 s reaches ~96% of the cap
    constexpr double SMOKE_CRUISE_MIN_ALIGNMENT = 0.99;      // Relative velocity points along forward
    constexpr double SMOKE_THRUST_GAIN_TOLERANCE_MPS = 10.0; // Forward gain vs 50 m/s^2 * 5 s (gravity is ~1.5 m/s^2)
    constexpr double SMOKE_COAST_MAX_CHANGE = 0.05;          // Coasting keeps the speed (only gravity changes it)
    constexpr double SMOKE_PITCH_MIN_ANGLE_DEG = 120.0;      // 90 deg/s for 2 s, minus the 0.15 s response lag
    constexpr double SMOKE_BRAKE_MAX_REL_MPS = 2.0;          // Assist braked to the reference frame
    constexpr double SMOKE_CONTACT_ALTITUDE_TOLERANCE_M = 0.5;
    constexpr double SMOKE_MAX_INWARD_MPS = 0.01;            // Velocity into the surface is removed
    constexpr double SMOKE_WARP_MAX_ALTITUDE_DRIFT_M = 50.0; // Hands-off hover co-moves with Earth under 1 d/s
    constexpr double SMOKE_WARP_MAX_REL_MPS = 5.0;           // ...and stays (nearly) at rest relative to it
    constexpr double SMOKE_SURFACE_MAX_REL_MPS = 0.1;        // Resting on the surface under warp: no slide, no bounce
}

//////////////////////////////////////////////////////////////////////////
// Starts the first phase
void FSOLShipSmokeFlight::Start(USOLShipSubsystem& ships, const USOLAnchorSubsystem& anchor,
    const FSOLBodyRegistry& registry)
{
    UE_LOG(LogSOL, Log, TEXT("SmokeFlight: scripted flight starting"));
    mTotalTimeS = 0.0;
    mChecksPassed = 0;
    mChecksRun = 0;
    BeginPhase(ESOLSmokePhase::Hover, ships, anchor, registry);
}

//////////////////////////////////////////////////////////////////////////
// Advances the script after a ship step by the frame's real delta; returns true once every phase has finished
bool FSOLShipSmokeFlight::Update(USOLShipSubsystem& ships, const USOLAnchorSubsystem& anchor,
    const FSOLBodyRegistry& registry, const float realDeltaSeconds)
{
    if (mPhase == ESOLSmokePhase::Done)
    {
        return true;
    }
    mPhaseTimeS += realDeltaSeconds;
    mTotalTimeS += realDeltaSeconds;

    // The warp phases watch every frame, not just the checkpoints
    if (mPhase == ESOLSmokePhase::Warp || mPhase == ESOLSmokePhase::Surface)
    {
        TrackWarpPhase(Sample(ships, registry), anchor, registry);
    }

    // Periodic checkpoint while the phase runs
    if (mPhaseTimeS >= mNextLogTimeS && mPhaseTimeS < GetPhaseDuration(mPhase))
    {
        LogSample(GetPhaseName(mPhase), Sample(ships, registry), ships, anchor, registry);
        mNextLogTimeS += SMOKE_LOG_INTERVAL_S;
    }
    if (mPhaseTimeS < GetPhaseDuration(mPhase))
    {
        return false;
    }

    // Phase over: final checkpoint, verdict, then the next phase
    const FSOLSmokeSample endSample = Sample(ships, registry);
    LogSample(GetPhaseName(mPhase), endSample, ships, anchor, registry);
    EndPhase(endSample, ships, registry);
    BeginPhase(static_cast<ESOLSmokePhase>(static_cast<uint8>(mPhase) + 1), ships, anchor, registry);
    if (mPhase != ESOLSmokePhase::Done)
    {
        return false;
    }
    SetWarpIndex(ships, 0);
    UE_LOG(LogSOL, Log, TEXT("SmokeFlight: warp reset to 1x"));
    UE_LOG(LogSOL, Log, TEXT("SmokeFlight: complete after %.1f s, %d/%d checks passed%s"), mTotalTimeS,
        mChecksPassed, mChecksRun, mChecksPassed == mChecksRun ? TEXT("") : TEXT(" (FAILURES)"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Measures the ship now
FSOLShipSmokeFlight::FSOLSmokeSample FSOLShipSmokeFlight::Sample(const USOLShipSubsystem& ships,
    const FSOLBodyRegistry& registry)
{
    FSOLSmokeSample sample;
    const FSOLShipState state = ships.GetState();
    sample.RelativeVelocityMps = state.VelocityMps - ships.GetReferenceVelocityMps();
    sample.AbsoluteSpeedMps = state.VelocityMps.Size();
    sample.ForwardDir = state.Orientation.GetForwardVector();
    sample.UpDir = state.Orientation.GetUpVector();
    sample.NearestBody = ships.FindNearestBody(sample.AltitudeM);
    if (sample.NearestBody != INDEX_NONE)
    {
        const FVector3d bodyM = SOLRender::EclipticToUnreal(registry.GetPositionM(sample.NearestBody));
        sample.RadialDir = (state.PositionM - bodyM).GetSafeNormal();
    }
    return sample;
}

//////////////////////////////////////////////////////////////////////////
// Logs one checkpoint line
void FSOLShipSmokeFlight::LogSample(const TCHAR* label, const FSOLSmokeSample& sample, const USOLShipSubsystem& ships,
    const USOLAnchorSubsystem& anchor, const FSOLBodyRegistry& registry) const
{
    const int32 anchorIndex = anchor.GetAnchorIndex();
    const int32 contact = ships.GetContactBodyIndex();
    UE_LOG(LogSOL, Log, TEXT("SmokeFlight %-12s t=%6.2f phase=%5.2f rel=%10.3f m/s abs=%9.1f m/s alt=%12.1f m above %s "
        "radialRel=%9.3f m/s fwd=(%.3f,%.3f,%.3f) assist=%s anchor=%s contact=%s"), label, mTotalTimeS, mPhaseTimeS,
        sample.RelativeVelocityMps.Size(), sample.AbsoluteSpeedMps, sample.AltitudeM,
        sample.NearestBody == INDEX_NONE ? TEXT("none") : *registry.GetName(sample.NearestBody).ToString(),
        FVector3d::DotProduct(sample.RelativeVelocityMps, sample.RadialDir), sample.ForwardDir.X, sample.ForwardDir.Y,
        sample.ForwardDir.Z, ships.GetControl().bFlightAssist ? TEXT("on") : TEXT("off"),
        anchorIndex == INDEX_NONE ? TEXT("none") : *registry.GetName(anchorIndex).ToString(),
        contact == INDEX_NONE ? TEXT("none") : *registry.GetName(contact).ToString());
}

//////////////////////////////////////////////////////////////////////////
// Sets up the control (and for the collision phase, the ship's state) for a phase
void FSOLShipSmokeFlight::BeginPhase(const ESOLSmokePhase phase, USOLShipSubsystem& ships,
    const USOLAnchorSubsystem& anchor, const FSOLBodyRegistry& registry)
{
    mPhase = phase;
    mPhaseTimeS = 0.0;
    mNextLogTimeS = SMOKE_LOG_INTERVAL_S;
    FSOLShipControl control = ships.GetControl();
    control.Thrust = FVector3d::ZeroVector;
    control.Rotation = FVector3d::ZeroVector;
    control.bBoost = false;

    // Per-phase inputs
    switch (phase)
    {
    case ESOLSmokePhase::Hover:
    case ESOLSmokePhase::Brake:
        control.bFlightAssist = true;
        break;
    case ESOLSmokePhase::Warp:
        // Hands off in assist at 1 day per second: the ship must co-move with Earth, not fall behind it
        control.bFlightAssist = true;
        SetWarpIndex(ships, SMOKE_WARP_INDEX);
        break;
    case ESOLSmokePhase::Surface:
        // Hands off in Newtonian flight, still at 1 d/s: gravity keeps pressing the ship onto the surface
        control.bFlightAssist = false;
        break;
    case ESOLSmokePhase::Cruise:
        control.bFlightAssist = true;
        control.SpeedCapMps = SMOKE_CRUISE_CAP_MPS;
        control.Thrust = FVector3d(1.0, 0.0, 0.0);
        break;
    case ESOLSmokePhase::NewtonThrust:
        control.bFlightAssist = false;
        control.Thrust = FVector3d(1.0, 0.0, 0.0);
        break;
    case ESOLSmokePhase::NewtonCoast:
        control.bFlightAssist = false;
        break;
    case ESOLSmokePhase::Pitch:
        control.bFlightAssist = false;
        control.Rotation = FVector3d(0.0, 1.0, 0.0);
        break;
    case ESOLSmokePhase::Collide:
    {
        // Teleport a few km above Earth at rest relative to it, nose down, then boost straight into the surface
        const int32 earth = registry.FindByName(FName(SOL::BodyNames::EARTH));
        const FVector3d earthM = SOLRender::EclipticToUnreal(registry.GetPositionM(earth));
        FVector3d radial = (ships.GetState().PositionM - earthM).GetSafeNormal();
        if (radial.IsNearlyZero())
        {
            radial = FVector3d::ZAxisVector;
        }
        FSOLShipState state;
        state.PositionM = earthM + radial * (registry.GetRadiusM(earth) + SMOKE_COLLIDE_START_ALTITUDE_M);
        state.VelocityMps = SOLRender::EclipticToUnreal(registry.GetVelocityMps(earth));
        state.Orientation = UE::Math::TRotationMatrix<double>::MakeFromX(-radial).ToQuat();
        ships.SetState(state);
        control.bFlightAssist = false;
        control.bBoost = true;
        control.Thrust = FVector3d(1.0, 0.0, 0.0);
        break;
    }
    case ESOLSmokePhase::Done:
        control.bFlightAssist = true;
        break;
    }
    ships.SetControl(control);
    if (phase != ESOLSmokePhase::Done)
    {
        mPhaseStart = Sample(ships, registry);
        LogSample(GetPhaseName(phase), mPhaseStart, ships, anchor, registry);
    }

    // The warp phases hold an altitude: the hover keeps its start altitude, the rest sits at the ship radius
    mReferenceAltitudeM = phase == ESOLSmokePhase::Surface ? ships.GetFlightParams().ShipRadiusM
        : mPhaseStart.AltitudeM;
    mMaxAltitudeDriftM = 0.0;
    mAnchorStayedEarth = true;
}

//////////////////////////////////////////////////////////////////////////
// Sets the sim clock's time-warp step (0 = 1x) through the clock's public stepping API
void FSOLShipSmokeFlight::SetWarpIndex(const USOLShipSubsystem& ships, const int32 warpIndex)
{
    USOLSimClockSubsystem* clock = ships.GetWorld()->GetSubsystem<USOLSimClockSubsystem>();
    if (clock == nullptr)
    {
        return;
    }
    clock->ResetWarp();
    for (int32 step = 0; step < warpIndex; ++step)
    {
        clock->StepWarpUp();
    }
    UE_LOG(LogSOL, Log, TEXT("SmokeFlight: warp index %d (x%.0f)"), clock->GetClock().GetWarpIndex(),
        clock->GetClock().GetWarpFactor());
}

//////////////////////////////////////////////////////////////////////////
// Tracks the worst altitude drift and whether the anchor stayed Earth during the warp phases
void FSOLShipSmokeFlight::TrackWarpPhase(const FSOLSmokeSample& sample, const USOLAnchorSubsystem& anchor,
    const FSOLBodyRegistry& registry)
{
    mMaxAltitudeDriftM = FMath::Max(mMaxAltitudeDriftM, FMath::Abs(sample.AltitudeM - mReferenceAltitudeM));
    if (anchor.GetAnchorIndex() != registry.FindByName(FName(SOL::BodyNames::EARTH)))
    {
        mAnchorStayedEarth = false;
    }
}

//////////////////////////////////////////////////////////////////////////
// Evaluates and logs the verdict of the phase that just ended
void FSOLShipSmokeFlight::EndPhase(const FSOLSmokeSample& sample, const USOLShipSubsystem& ships,
    const FSOLBodyRegistry& registry)
{
    const double relSpeed = sample.RelativeVelocityMps.Size();
    const double startRelSpeed = mPhaseStart.RelativeVelocityMps.Size();
    bool bPassed = false;
    FString detail;

    // Each phase has one expectation, measured against the sample taken when the phase began
    switch (mPhase)
    {
    case ESOLSmokePhase::Hover:
        bPassed = relSpeed < SMOKE_HOVER_MAX_REL_MPS;
        detail = FString::Printf(TEXT("relative speed %.1f -> %.3f m/s (limit %.1f)"), startRelSpeed, relSpeed,
            SMOKE_HOVER_MAX_REL_MPS);
        break;
    case ESOLSmokePhase::Cruise:
    {
        const double alignment = FVector3d::DotProduct(sample.RelativeVelocityMps.GetSafeNormal(), sample.ForwardDir);
        bPassed = relSpeed >= SMOKE_CRUISE_MIN_REL_MPS && relSpeed <= SMOKE_CRUISE_CAP_MPS + 1.0
            && alignment >= SMOKE_CRUISE_MIN_ALIGNMENT;
        detail = FString::Printf(TEXT("relative speed %.2f m/s at cap %.0f, alignment with forward %.4f"), relSpeed,
            SMOKE_CRUISE_CAP_MPS, alignment);
        break;
    }
    case ESOLSmokePhase::NewtonThrust:
    {
        const double expectedGain = ships.GetFlightParams().NewtonianMainAccelMps2 * SMOKE_NEWTON_THRUST_S;
        const double gain = FVector3d::DotProduct(sample.RelativeVelocityMps - mPhaseStart.RelativeVelocityMps,
            mPhaseStart.ForwardDir);
        bPassed = FMath::Abs(gain - expectedGain) <= SMOKE_THRUST_GAIN_TOLERANCE_MPS;
        detail = FString::Printf(TEXT("forward speed gain %.2f m/s (expected ~%.0f over %.2f s real)"), gain,
            expectedGain, mPhaseTimeS);
        break;
    }
    case ESOLSmokePhase::NewtonCoast:
        bPassed = FMath::Abs(relSpeed - startRelSpeed) <= SMOKE_COAST_MAX_CHANGE * startRelSpeed;
        detail = FString::Printf(TEXT("relative speed %.2f -> %.2f m/s while coasting"), startRelSpeed, relSpeed);
        break;
    case ESOLSmokePhase::Pitch:
    {
        const double angleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
            FVector3d::DotProduct(sample.ForwardDir, mPhaseStart.ForwardDir), -1.0, 1.0)));
        const double towardUp = FVector3d::DotProduct(sample.ForwardDir, mPhaseStart.UpDir);
        bPassed = angleDeg >= SMOKE_PITCH_MIN_ANGLE_DEG && towardUp > 0.0;
        detail = FString::Printf(TEXT("forward rotated %.1f deg, toward the old up %.3f"), angleDeg, towardUp);
        break;
    }
    case ESOLSmokePhase::Brake:
        bPassed = relSpeed < SMOKE_BRAKE_MAX_REL_MPS;
        detail = FString::Printf(TEXT("relative speed %.1f -> %.3f m/s (limit %.1f)"), startRelSpeed, relSpeed,
            SMOKE_BRAKE_MAX_REL_MPS);
        break;
    case ESOLSmokePhase::Collide:
    {
        const double shipRadiusM = ships.GetFlightParams().ShipRadiusM;
        const int32 earth = registry.FindByName(FName(SOL::BodyNames::EARTH));
        const double inwardMps = -FVector3d::DotProduct(sample.RelativeVelocityMps, sample.RadialDir);
        bPassed = sample.NearestBody == earth && ships.GetContactBodyIndex() == earth
            && FMath::Abs(sample.AltitudeM - shipRadiusM) <= SMOKE_CONTACT_ALTITUDE_TOLERANCE_M
            && inwardMps <= SMOKE_MAX_INWARD_MPS;
        detail = FString::Printf(TEXT("altitude %.3f m (ship radius %.1f), inward relative speed %.4f m/s, contact %s"),
            sample.AltitudeM, shipRadiusM, inwardMps, ships.GetContactBodyIndex() == earth ? TEXT("Earth")
            : TEXT("none"));
        break;
    }
    case ESOLSmokePhase::Warp:
    {
        const int32 earth = registry.FindByName(FName(SOL::BodyNames::EARTH));
        bPassed = sample.NearestBody == earth && mAnchorStayedEarth
            && mMaxAltitudeDriftM < SMOKE_WARP_MAX_ALTITUDE_DRIFT_M && relSpeed < SMOKE_WARP_MAX_REL_MPS;
        detail = FString::Printf(TEXT("1 d/s hands-off: altitude %.1f -> %.1f m (max drift %.3f m, limit %.0f), "
            "relative speed %.4f m/s (limit %.0f), anchor stayed Earth %s"), mPhaseStart.AltitudeM, sample.AltitudeM,
            mMaxAltitudeDriftM, SMOKE_WARP_MAX_ALTITUDE_DRIFT_M, relSpeed, SMOKE_WARP_MAX_REL_MPS,
            mAnchorStayedEarth ? TEXT("yes") : TEXT("NO"));
        break;
    }
    case ESOLSmokePhase::Surface:
    {
        const int32 earth = registry.FindByName(FName(SOL::BodyNames::EARTH));
        bPassed = sample.NearestBody == earth && ships.GetContactBodyIndex() == earth && mAnchorStayedEarth
            && mMaxAltitudeDriftM <= SMOKE_CONTACT_ALTITUDE_TOLERANCE_M && relSpeed <= SMOKE_SURFACE_MAX_REL_MPS;
        detail = FString::Printf(TEXT("1 d/s resting: altitude %.4f m (ship radius %.1f, max deviation %.4f m, limit "
            "%.1f), relative speed %.5f m/s (limit %.1f), contact %s, anchor stayed Earth %s"), sample.AltitudeM,
            mReferenceAltitudeM, mMaxAltitudeDriftM, SMOKE_CONTACT_ALTITUDE_TOLERANCE_M, relSpeed,
            SMOKE_SURFACE_MAX_REL_MPS, ships.GetContactBodyIndex() == earth ? TEXT("Earth") : TEXT("none"),
            mAnchorStayedEarth ? TEXT("yes") : TEXT("NO"));
        break;
    }
    case ESOLSmokePhase::Done:
        return;
    }

    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SmokeFlight CHECK %-12s %s: %s"), GetPhaseName(mPhase), bPassed ? TEXT("PASS")
        : TEXT("FAIL"), *detail);
}

//////////////////////////////////////////////////////////////////////////
// Returns the phase's scripted duration in seconds
double FSOLShipSmokeFlight::GetPhaseDuration(const ESOLSmokePhase phase)
{
    switch (phase)
    {
    case ESOLSmokePhase::Hover:
        return SMOKE_HOVER_S;
    case ESOLSmokePhase::Cruise:
        return SMOKE_CRUISE_S;
    case ESOLSmokePhase::NewtonThrust:
        return SMOKE_NEWTON_THRUST_S;
    case ESOLSmokePhase::NewtonCoast:
        return SMOKE_NEWTON_COAST_S;
    case ESOLSmokePhase::Pitch:
        return SMOKE_PITCH_S;
    case ESOLSmokePhase::Brake:
        return SMOKE_BRAKE_S;
    case ESOLSmokePhase::Warp:
        return SMOKE_WARP_S;
    case ESOLSmokePhase::Collide:
        return SMOKE_COLLIDE_S;
    case ESOLSmokePhase::Surface:
        return SMOKE_SURFACE_S;
    default:
        return 0.0;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the phase's display name
const TCHAR* FSOLShipSmokeFlight::GetPhaseName(const ESOLSmokePhase phase)
{
    switch (phase)
    {
    case ESOLSmokePhase::Hover:
        return TEXT("a-Hover");
    case ESOLSmokePhase::Cruise:
        return TEXT("b-Cruise");
    case ESOLSmokePhase::NewtonThrust:
        return TEXT("c-Thrust");
    case ESOLSmokePhase::NewtonCoast:
        return TEXT("c-Coast");
    case ESOLSmokePhase::Pitch:
        return TEXT("d-Pitch");
    case ESOLSmokePhase::Brake:
        return TEXT("e-Brake");
    case ESOLSmokePhase::Warp:
        return TEXT("f-Warp");
    case ESOLSmokePhase::Collide:
        return TEXT("g-Collide");
    case ESOLSmokePhase::Surface:
        return TEXT("h-Surface");
    default:
        return TEXT("Done");
    }
}
