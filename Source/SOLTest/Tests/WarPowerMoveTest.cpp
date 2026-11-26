/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Flight/SOLChase.h"
#include "Flight/SOLFlight.h"
#include "Flight/SOLNewtonianFlight.h"
#include "Flight/SOLShipState.h"
#include "Interaction/SOLActionTable.h"
#include "Menu/SOLMenuText.h"
#include "SOLConstants.h"
#include "Ship/SOLAutopilotOrder.h"
#include "Ship/SOLShipFragments.h"

#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #50 Part 50d "War power move" (SDD 50 D19, D22, section 4.9.1, contract case 14), written
// against that contract before the production code existed (part 50d, test first): the round command menu row next to
// Move To, the per-command ignore-lanes flag (reset by every other order, never saved) and the flight at full thrust.
// The 50d review-fix round re-specified it test-first: War power flies PULSES (user decision 2026-11-17: full boosted
// pulses against recovery, not one burst then the sustained limit), a running Move To no longer blocks War power to
// the same body or the other way round (user decision: the new command replaces the old), and the HUD shows boost and
// a War power order. The lane side (no lane logic for a War power move) is in LaneAutoRulesTest.cpp.
//
// API contract (from the test author; names the implementer follows):
//
// Interaction/SOLActionTable.h: ESOLAction::MoveToWarPower, appended after Trade (just before Count) so no existing
//   ordinal moves (nothing persists ESOLAction: no save, settings or key-binding file names it); the row sits right
//   after MoveTo wherever MoveTo is offered (Star, Planet, Moon, Dwarf live; Asteroid a placeholder, as its MoveTo);
//   SOLActionTable::Evaluate gives MoveTo and MoveToWarPower the same rules: NoShip, JumpWarping, NoPilotControl and
//   nothing else (neither row returns AlreadyMovingTo any more, whatever bIsMoveTarget says: a new Move To or War
//   power order replaces the running one; the enum value and its text stay); ActionLabel returns its
//   SOLMenuText::INTERACT_ACTION_LABELS entry (the label is the implementer's, short, D22)
//
// Ship/SOLShipFragments.h: FSOLShipControlFragment gains `bool bAutopilotIgnoreLanes = false;` (transient, not a
//   UPROPERTY, never saved: SAVE_VERSION 13 and KEY_BINDING_LAYOUT_VERSION 10 unchanged, D11); FSOLShipStateFragment
//   gains `FSOLWarPowerPulseState WarPowerPulse;` (transient, not reflected, never saved; the processor resets it with
//   each new order)
//
// Ship/SOLAutopilotOrder.h (pure; USOLShipSubsystem::EngageAutopilot / EngageAutopilotPoint / EngageChase,
// DisengageAutopilot and ClearAutopilotOrder write the order through these, so "per command" holds without a world):
//   SOLAutopilotOrder::WriteBodyOrder(FSOLShipControlFragment& control, int32 bodyIndex, const FVector3d& towardShipM,
//       bool bWarPower): AutopilotBodyIndex = bodyIndex, AutopilotOffsetDir = towardShipM.GetSafeNormal(
//       UE_DOUBLE_SMALL_NUMBER, +Z), bAutopilotPoint = false, bAutopilotIgnoreLanes = bWarPower
//   SOLAutopilotOrder::WritePointOrder(FSOLShipControlFragment& control, int32 bodyIndex, const FVector3d& offsetM,
//       double passSpeedMps, double captureRadiusM): AutopilotBodyIndex, AutopilotPointOffsetM, max(pass, 0),
//       max(capture, 0), bAutopilotPoint = true, bAutopilotIgnoreLanes = false
//   SOLAutopilotOrder::ClearOrder(FSOLShipControlFragment& control): AutopilotBodyIndex = INDEX_NONE, bAutopilotPoint =
//       false, bAutopilotIgnoreLanes = false (a chase, a disengage, a reset)
//   SOLAutopilotOrder::IsBoostShown(bool bBoostSwitchOn, bool bWarPowerOrder) -> the HUD boost indicator: on while the
//       boost switch is on or a War power order flies (it flies boosted)
//   SOLAutopilotOrder::AutopilotActivityText(bool bWarPowerOrder) -> SOLMenuText::AUTOPILOT_WAR_POWER_ACTIVITY for a
//       War power order (the AUTOPILOT marker's activity line marks it), else an empty string
//   USOLShipSubsystem::EngageAutopilot(int32 bodyIndex, bool bWarPower = false); USOLInteractionSubsystem::Execute's
//       MoveToWarPower case calls it with true
//
// Flight/SOLFlight.h:
//   SOLFlight::WarPowerAccelLimitMps2(const FSOLThrustEnvelope& env, bool bBoost) -> the maximum thrust, 100 % effort:
//       SOLNewtonian::LocalThrustAccelMps2(FVector3d(1, 0, 0), env, bBoost).X (full forward fraction, boost when on,
//       the total cap applied)
//   struct FSOLWarPowerPulseState { bool bRecovering = false; } (transient: the pulse phase, never saved)
//   SOLFlight::WarPowerPulseAccelMps2(const FSOLThrustEnvelope& env, const FSOLPilotGLimits& limits,
//       const FSOLGMeterState& meter, bool bBoost, double dt, FSOLWarPowerPulseState& inOutPulse,
//       double pulseG = SOL::WAR_POWER_PULSE_G) -> the acceleration War power flies this substep, updating the phase:
//       pulsing switches to recovering at a meter <= SOL::WAR_POWER_BAND_LOW_METER (or in G-LOC), recovering switches
//       to pulsing at a meter >= SOL::WAR_POWER_BAND_HIGH_METER; recovering -> min(the sustained G x g0,
//       WarPowerAccelLimitMps2(env, bBoost)); pulsing -> min(pulseG x g0, WarPowerAccelLimitMps2(env, bBoost), the
//       meter-safe G for the step above the band's low edge (SOLChase::MaxSafeGForStep) x g0), so the meter never
//       drops below the hard floor SOL::WAR_POWER_METER_FLOOR at any substep length
//   SOLFlight::MakeWarPowerPulseGuidanceParams(const FSOLThrustEnvelope& env, const FSOLPilotGLimits& limits,
//       const FSOLGMeterState& meter, double gravityMps2, bool bBoost, double stepLimitMps2) -> PlanAccelMps2 at
//       least that of SOLNewtonian::MakeGuidanceParams(env, stepLimitMps2, gravityMps2, true, bBoost) and high enough
//       that the accelerate demand reaches stepLimitMps2 (the guidance's 0.9 margin is not taken off the pulse: the
//       normal AutopilotStep accelerates at 0.9 x its limit, 1.35 G); BrakeAccelMps2 exactly that of
//       MakeGuidanceParams(env, SOLNewtonian::AutopilotAccelLimitMps2(limits, meter), gravityMps2, true, false) (the
//       normal autopilot's brake plan, about 1.35 G); the other fields as the plan call
//   SOLFlight::WarPowerPulseAutopilotStep(state, targetPositionM, targetVelocityMps, targetAccelMps2, gravityAccelMps2,
//       params, meter, bool bBoost, dt, FSOLWarPowerPulseState& inOutPulse, outStep = nullptr, outGuidance = nullptr,
//       targetGravityMps2 = 0.0) -> AutopilotStep's rendezvous with the limit from one WarPowerPulseAccelMps2 call
//       (which advances inOutPulse), the budgets of MakeWarPowerPulseGuidanceParams and the clamp at that limit, so
//       far from the target it achieves that acceleration (never more); no lane logic; AutopilotStep itself
//       unchanged. The War power move flies with boost on
//   Replaced, no test uses them any more: WarPowerStepAccelLimitMps2, MakeWarPowerGuidanceParams,
//       WarPowerAutopilotStep (burst then hold)
//
// SOLConstants.h: SOL::WAR_POWER_METER_FLOOR stays 0.3 (its static_assert keeps it in (0.25, 0.35]); new
//   SOL::WAR_POWER_PULSE_G = SustainedG + (MaxG - SustainedG) sqrt(0.5), about 6.80 G (its drain equals the 0.05 / s
//   recovery, so it pulses half the time and averages about 4.15 G), SOL::WAR_POWER_BAND_LOW_METER =
//   PILOT_VISION_START_METER (0.4, no grey-out) and SOL::WAR_POWER_BAND_HIGH_METER = 0.6.
// Menu/SOLMenuText.h: AUTOPILOT_WAR_POWER_ACTIVITY (short, upper case; the wording the implementer's).
//
// Expected trip (SDD 50 review numbers, flip and burn over 2.25e11 m from rest, braking at the normal plan): War power
// about 59 h up to 10 s substeps (burst then hold was 70.6 h, plain AUTO 72.4 h), about 61 h at 30 s and 66 h at
// 169 s substeps (loose bands), never slower than plain AUTO; a 9 G pulse averages about 4.0 G, worse than 6.8 G.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // The body types whose Move To row is live
    const ESOLObjectType WARPOWER_BODY_TYPES[] = { ESOLObjectType::Star, ESOLObjectType::Planet, ESOLObjectType::Moon,
        ESOLObjectType::Dwarf };

    // Context flags of FSOLActionContext (bit 0 bHasShip ... bit 8 bIsChased)
    constexpr int32 WARPOWER_CONTEXT_FLAGS = 9;

    //////////////////////////////////////////////////////////////////////////
    // Returns the context whose flags are the bits of mask (bit 0 bHasShip ... bit 7 bIsFollowed, bit 8 bIsChased)
    FSOLActionContext WarPowerContextFromMask(const int32 mask)
    {
        FSOLActionContext context;
        context.bHasShip = (mask & (1 << 0)) != 0;
        context.bPilotHasControl = (mask & (1 << 1)) != 0;
        context.bJumpWarping = (mask & (1 << 2)) != 0;
        context.bIsTargetable = (mask & (1 << 3)) != 0;
        context.bIsSelected = (mask & (1 << 4)) != 0;
        context.bIsFrameLocked = (mask & (1 << 5)) != 0;
        context.bIsMoveTarget = (mask & (1 << 6)) != 0;
        context.bIsFollowed = (mask & (1 << 7)) != 0;
        context.bIsChased = (mask & (1 << 8)) != 0;
        return context;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the row index of an action in a type's favourable menu, or INDEX_NONE
    int32 WarPowerRowOf(const ESOLObjectType type, const ESOLAction action)
    {
        FSOLActionContext favourable;
        favourable.bHasShip = true;
        favourable.bPilotHasControl = true;
        favourable.bIsTargetable = true;
        FSOLActionList list;
        SOLActionTable::Build(type, favourable, list);
        for (int32 row = 0; row < list.Num; ++row)
        {
            if (list.Entries[row].Action == action)
            {
                return row;
            }
        }
        return INDEX_NONE;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a fragment holding a stale order of every kind's fields (so a write must set what it owns)
    FSOLShipControlFragment WarPowerStaleControl()
    {
        FSOLShipControlFragment control;
        control.AutopilotBodyIndex = 7;
        control.AutopilotOffsetDir = FVector3d(0.0, 1.0, 0.0);
        control.AutopilotPointOffsetM = FVector3d(1.0, 2.0, 3.0);
        control.AutopilotPassSpeedMps = 123.0;
        control.AutopilotCaptureRadiusM = 456.0;
        control.bAutopilotPoint = true;
        control.bAutopilotIgnoreLanes = true;
        return control;
    }

    // Everything one far-target step of an autopilot reports
    struct FWarPowerRun
    {
        FSOLShipState State;
        FSOLFlightStepOutput Step;
        FSOLGuidanceOutput Guidance;
    };

    //////////////////////////////////////////////////////////////////////////
    // Runs one one-second step from rest at the origin, nose on +X, toward a far still target on +X (accelerate
    // phase), with the given meter, through the War power pulse step (advancing the pulse state) or the normal step
    FWarPowerRun WarPowerFarStep(const bool bWarPower, const bool bBoost, const FSOLGMeterState& meter,
        FSOLWarPowerPulseState& inOutPulse)
    {
        const FSOLFlightParams params;
        FWarPowerRun run;
        run.State.Orientation = FQuat4d::Identity;
        const FVector3d targetM(1.0e10, 0.0, 0.0);
        run.State = bWarPower
            ? SOLFlight::WarPowerPulseAutopilotStep(run.State, targetM, FVector3d::ZeroVector, FVector3d::ZeroVector,
                FVector3d::ZeroVector, params, meter, bBoost, 1.0, inOutPulse, &run.Step, &run.Guidance)
            : SOLFlight::AutopilotStep(run.State, targetM, FVector3d::ZeroVector, FVector3d::ZeroVector,
                FVector3d::ZeroVector, params, meter, 1.0, &run.Step, &run.Guidance);
        return run;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a conscious meter at a level
    FSOLGMeterState WarPowerMeter(const double level)
    {
        FSOLGMeterState meter;
        meter.Meter = level;
        return meter;
    }

    // The Earth - Mars scale flip-and-burn distance of the SDD's trip numbers
    constexpr double WARPOWER_TRIP_M = 2.25e11;
    constexpr double WARPOWER_HOUR_S = 3600.0;

    // One flight's outcome
    struct FWarPowerTrip
    {
        bool bArrived = false;          // The guidance reported Arrived
        double ArrivalS = 0.0;          // ...at this sim time
        double CloseS = 0.0;            // When the distance first fell to 1e-4 of the trip (0: never)
        double MinMeter = 1.0;          // Lowest meter
        bool bBlackout = false;         // Any G-LOC
        double MaxOvershootM = 0.0;     // Furthest past the target (>= 0)
    };

    //////////////////////////////////////////////////////////////////////////
    // Flies from rest at the origin, nose on +X, to a still target distanceM away on +X through the War power pulse
    // step with boost (bWarPower) or the normal AutopilotStep, the G meter stepped from the achieved thrust as the
    // flight processor does, until the guidance arrives or maxS
    FWarPowerTrip WarPowerFly(const bool bWarPower, const double distanceM, const double dt, const double maxS)
    {
        const FSOLFlightParams params;
        const FVector3d zero = FVector3d::ZeroVector;
        const FVector3d targetM(distanceM, 0.0, 0.0);
        FWarPowerTrip trip;
        FSOLShipState state;
        state.Orientation = FQuat4d::Identity;
        FSOLGMeterState meter;
        FSOLWarPowerPulseState pulse;
        const int32 steps = FMath::CeilToInt(maxS / dt);
        for (int32 index = 0; index < steps; ++index)
        {
            FSOLFlightStepOutput step;
            FSOLGuidanceOutput guidance;
            state = bWarPower
                ? SOLFlight::WarPowerPulseAutopilotStep(state, targetM, zero, zero, zero, params, meter, true, dt,
                    pulse, &step, &guidance)
                : SOLFlight::AutopilotStep(state, targetM, zero, zero, zero, params, meter, dt, &step, &guidance);
            meter = SOLNewtonian::StepGMeter(meter, SOLNewtonian::GLoadG(step.ThrustAccelWorldMps2), params.PilotLimits,
                dt);
            trip.MinMeter = FMath::Min(trip.MinMeter, meter.Meter);
            trip.bBlackout = trip.bBlackout || meter.bBlackout;
            trip.MaxOvershootM = FMath::Max(trip.MaxOvershootM, state.PositionM.X - distanceM);
            const double timeS = dt * (index + 1);
            if (trip.CloseS <= 0.0 && (targetM - state.PositionM).Size() <= 1.0e-4 * distanceM)
            {
                trip.CloseS = timeS;
            }
            if (guidance.Phase == ESOLGuidancePhase::Arrived)
            {
                trip.bArrived = true;
                trip.ArrivalS = timeS;
                break;
            }
        }
        return trip;
    }

    // An accelerate-only run's meter and thrust record
    struct FWarPowerBurn
    {
        double MinMeter = 1.0;          // Lowest meter over the whole run
        double MinMeterAfter60 = 1.0;   // Lowest meter after the first 60 s (the startup burst)
        double MaxMeterAfter60 = 0.0;   // Highest meter after the first 60 s
        double AverageG = 0.0;          // Mean achieved thrust after the first 60 s (G)
        double PulseFraction = 0.0;     // Share of that time thrusting above 2 G
        bool bBlackout = false;         // Any G-LOC
        bool bCritical = false;         // Any critical, blackout or lethal warning
    };

    //////////////////////////////////////////////////////////////////////////
    // Flies durationS of War power (boost on) from rest toward a target too far to brake for, the meter stepped from
    // the achieved thrust, and records the meter band and the mean thrust
    FWarPowerBurn WarPowerBurnRun(const double dt, const double durationS)
    {
        const FSOLFlightParams params;
        const double g0 = SOLNewtonian::STANDARD_GRAVITY_MPS2;
        const FVector3d zero = FVector3d::ZeroVector;
        const FVector3d targetM(1.0e13, 0.0, 0.0);
        FWarPowerBurn burn;
        FSOLShipState state;
        state.Orientation = FQuat4d::Identity;
        FSOLGMeterState meter;
        FSOLWarPowerPulseState pulse;
        double sumG = 0.0;
        double pulseS = 0.0;
        double countedS = 0.0;
        const int32 steps = FMath::CeilToInt(durationS / dt);
        for (int32 index = 0; index < steps; ++index)
        {
            FSOLFlightStepOutput step;
            state = SOLFlight::WarPowerPulseAutopilotStep(state, targetM, zero, zero, zero, params, meter, true, dt,
                pulse, &step);
            const double loadG = SOLNewtonian::GLoadG(step.ThrustAccelWorldMps2);
            meter = SOLNewtonian::StepGMeter(meter, loadG, params.PilotLimits, dt);
            burn.MinMeter = FMath::Min(burn.MinMeter, meter.Meter);
            burn.bBlackout = burn.bBlackout || meter.bBlackout;
            const ESOLGWarning warning = SOLNewtonian::GWarningLevel(meter, loadG, params.PilotLimits);
            burn.bCritical = burn.bCritical || warning == ESOLGWarning::Critical || warning == ESOLGWarning::Blackout
                || warning == ESOLGWarning::Lethal;
            if (dt * index >= 60.0)
            {
                burn.MinMeterAfter60 = FMath::Min(burn.MinMeterAfter60, meter.Meter);
                burn.MaxMeterAfter60 = FMath::Max(burn.MaxMeterAfter60, meter.Meter);
                sumG += step.ThrustAccelWorldMps2.Size() / g0 * dt;
                pulseS += step.ThrustAccelWorldMps2.Size() > 2.0 * g0 ? dt : 0.0;
                countedS += dt;
            }
        }
        burn.AverageG = countedS > 0.0 ? sumG / countedS : 0.0;
        burn.PulseFraction = countedS > 0.0 ? pulseS / countedS : 0.0;
        return burn;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the mean pulse limit (G) of a pure accelerate run after its first 60 s: WarPowerPulseAccelMps2 with the
    // given pulse level each substep, the meter stepped at that limit (the pulse level comparison)
    double WarPowerMeanPulseG(const double pulseG, const double dt, const double durationS)
    {
        const FSOLFlightParams params;
        const double g0 = SOLNewtonian::STANDARD_GRAVITY_MPS2;
        FSOLGMeterState meter;
        FSOLWarPowerPulseState pulse;
        double sumG = 0.0;
        double countedS = 0.0;
        const int32 steps = FMath::CeilToInt(durationS / dt);
        for (int32 index = 0; index < steps; ++index)
        {
            const double limitMps2 = SOLFlight::WarPowerPulseAccelMps2(params.Envelope, params.PilotLimits, meter, true,
                dt, pulse, pulseG);
            meter = SOLNewtonian::StepGMeter(meter, limitMps2 / g0, params.PilotLimits, dt);
            if (dt * index >= 60.0)
            {
                sumG += limitMps2 / g0 * dt;
                countedS += dt;
            }
        }
        return countedS > 0.0 ? sumG / countedS : 0.0;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarPowerActionRowTest, "SOLTest.WarPowerMove.ActionRow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// D22: the round menu's War power move row, appended to ESOLAction (no ordinal moves), right after Move To on every
// body (live) and on the asteroid (a placeholder, as its Move To); not on a ship or a dropped target; the body menus
// fill MAX_ACTIONS exactly
bool FSOLWarPowerActionRowTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLActionTable;
    TestEqual(TEXT("appended after Trade"), static_cast<int32>(ESOLAction::MoveToWarPower),
        static_cast<int32>(ESOLAction::Trade) + 1);
    TestEqual(TEXT("only the Sun safety row after it (issue #53)"), static_cast<int32>(ESOLAction::MoveToWarPower),
        static_cast<int32>(ESOLAction::Count) - 2);
    TestEqual(TEXT("MoveTo keeps its ordinal"), static_cast<int32>(ESOLAction::MoveTo), 2);

    for (const ESOLObjectType type : WARPOWER_BODY_TYPES)
    {
        const FString name = TypeName(type);
        TestTrue(name + TEXT(": offered"), Offers(type, ESOLAction::MoveToWarPower));
        TestFalse(name + TEXT(": live"), IsPlaceholder(type, ESOLAction::MoveToWarPower));
        const int32 moveRow = WarPowerRowOf(type, ESOLAction::MoveTo);
        TestTrue(name + TEXT(": Move To present"), moveRow != INDEX_NONE);
        TestEqual(name + TEXT(": right after Move To"), WarPowerRowOf(type, ESOLAction::MoveToWarPower), moveRow + 1);
        TestEqual(name + TEXT(": the menu fills MAX_ACTIONS"), RowCount(type), SOL::Interact::MAX_ACTIONS);
    }

    // The asteroid mirrors its Move To placeholder
    TestTrue(TEXT("Asteroid: offered"), Offers(ESOLObjectType::Asteroid, ESOLAction::MoveToWarPower));
    TestTrue(TEXT("Asteroid: a placeholder like its Move To"), IsPlaceholder(ESOLObjectType::Asteroid,
        ESOLAction::MoveToWarPower));
    TestEqual(TEXT("Asteroid: right after Move To"), WarPowerRowOf(ESOLObjectType::Asteroid,
        ESOLAction::MoveToWarPower), WarPowerRowOf(ESOLObjectType::Asteroid, ESOLAction::MoveTo) + 1);

    // Not where Move To is not
    TestFalse(TEXT("Ship: not offered"), Offers(ESOLObjectType::Ship, ESOLAction::MoveToWarPower));
    TestFalse(TEXT("DroppedTarget: not offered"), Offers(ESOLObjectType::DroppedTarget, ESOLAction::MoveToWarPower));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarPowerBlockRulesTest, "SOLTest.WarPowerMove.SameBlockRulesAsMoveTo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 50 section 4.9.1: the War power row follows Move To's block rules in every one of the 512 context combinations,
// on every type (placeholder and not-offered rows included)
bool FSOLWarPowerBlockRulesTest::RunTest(const FString& /*parameters*/)
{
    for (int32 typeIndex = 0; typeIndex < static_cast<int32>(ESOLObjectType::Count); ++typeIndex)
    {
        const ESOLObjectType type = static_cast<ESOLObjectType>(typeIndex);
        if (!SOLActionTable::Offers(type, ESOLAction::MoveTo))
        {
            continue;
        }
        int32 mismatches = 0;
        FString first;
        for (int32 mask = 0; mask < (1 << WARPOWER_CONTEXT_FLAGS); ++mask)
        {
            const FSOLActionContext context = WarPowerContextFromMask(mask);
            const ESOLActionBlock moveTo = SOLActionTable::Evaluate(type, ESOLAction::MoveTo, context);
            const ESOLActionBlock warPower = SOLActionTable::Evaluate(type, ESOLAction::MoveToWarPower, context);
            if (moveTo != warPower)
            {
                ++mismatches;
                first = first.IsEmpty() ? FString::Printf(TEXT("mask %d: %d vs %d"), mask,
                    static_cast<int32>(warPower), static_cast<int32>(moveTo)) : first;
            }
        }
        TestEqual(FString::Printf(TEXT("%s: War power blocks as Move To (%s)"), SOLActionTable::TypeName(type),
            *first), mismatches, 0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarPowerLabelTest, "SOLTest.WarPowerMove.Label",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// D22: the row's label comes from SOLMenuText, is short and differs from Move To's (the wording is the implementer's)
bool FSOLWarPowerLabelTest::RunTest(const FString& /*parameters*/)
{
    const int32 index = static_cast<int32>(ESOLAction::MoveToWarPower);
    TestEqual(TEXT("one label per action"), static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::INTERACT_ACTION_LABELS)),
        static_cast<int32>(ESOLAction::Count));
    const FString label = SOLActionTable::ActionLabel(ESOLAction::MoveToWarPower);
    TestFalse(TEXT("not empty"), label.TrimStartAndEnd().IsEmpty());
    TestTrue(TEXT("short (a menu label)"), label.Len() <= 20);
    TestNotEqual(TEXT("not Move To's"), label, FString(SOLActionTable::ActionLabel(ESOLAction::MoveTo)));
    if (index < static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::INTERACT_ACTION_LABELS)))
    {
        TestEqual(TEXT("from SOLMenuText"), label, FString(SOLMenuText::INTERACT_ACTION_LABELS[index]));
    }
    TestEqual(TEXT("Move To keeps its label"), FString(SOLActionTable::ActionLabel(ESOLAction::MoveTo)),
        FString(TEXT("Move To")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarPowerFlagNotSavedTest, "SOLTest.WarPowerMove.FlagDefaultAndNotSaved",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// D11 / D22: the ignore-lanes flag starts false, is a plain transient field (not a reflected property), and no save
// schema or key layout version moves
bool FSOLWarPowerFlagNotSavedTest::RunTest(const FString& /*parameters*/)
{
    TestFalse(TEXT("a new control fragment obeys the lanes"), FSOLShipControlFragment().bAutopilotIgnoreLanes);
    TestNull(TEXT("not a reflected property"), FindFProperty<FProperty>(FSOLShipControlFragment::StaticStruct(),
        TEXT("bAutopilotIgnoreLanes")));
    TestEqual(TEXT("SAVE_VERSION stays 13"), SOL::Settings::SAVE_VERSION, 13);
    TestEqual(TEXT("KEY_BINDING_LAYOUT_VERSION stays 10"), SOL::Settings::KEY_BINDING_LAYOUT_VERSION, 10);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarPowerPerCommandTest, "SOLTest.WarPowerMove.FlagPerCommand",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// D22 per command: only a War power body order sets the flag, and every other order (Move To, a route waypoint, a
// chase or a disengage) clears it, in any order; nothing is sticky
bool FSOLWarPowerPerCommandTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLAutopilotOrder;
    const FVector3d towardShipM(3.0, 4.0, 0.0);
    const FVector3d offsetM(1.0e6, -2.0e6, 3.0e5);

    // War power, then a normal Move To
    FSOLShipControlFragment control;
    WriteBodyOrder(control, 3, towardShipM, true);
    TestTrue(TEXT("War power: set"), control.bAutopilotIgnoreLanes);
    WriteBodyOrder(control, 3, towardShipM, false);
    TestFalse(TEXT("War power then Move To (same body): cleared"), control.bAutopilotIgnoreLanes);

    // Move To, then War power
    WriteBodyOrder(control, 4, towardShipM, false);
    TestFalse(TEXT("Move To: clear"), control.bAutopilotIgnoreLanes);
    WriteBodyOrder(control, 4, towardShipM, true);
    TestTrue(TEXT("Move To then War power: set"), control.bAutopilotIgnoreLanes);

    // War power twice stays set (each command asks for it)
    WriteBodyOrder(control, 5, towardShipM, true);
    TestTrue(TEXT("War power then War power: set"), control.bAutopilotIgnoreLanes);

    // War power, then a route waypoint
    WritePointOrder(control, 5, offsetM, 2500.0, 5.0e5);
    TestFalse(TEXT("War power then a route waypoint: cleared"), control.bAutopilotIgnoreLanes);

    // War power, then a chase or a disengage (both clear the order)
    WriteBodyOrder(control, 6, towardShipM, true);
    ClearOrder(control);
    TestFalse(TEXT("War power then a chase / disengage: cleared"), control.bAutopilotIgnoreLanes);

    // A stale flag never survives any write
    FSOLShipControlFragment stale = WarPowerStaleControl();
    WriteBodyOrder(stale, 1, towardShipM, false);
    TestFalse(TEXT("stale flag, Move To: cleared"), stale.bAutopilotIgnoreLanes);
    stale = WarPowerStaleControl();
    WritePointOrder(stale, 1, offsetM, 0.0, 0.0);
    TestFalse(TEXT("stale flag, waypoint: cleared"), stale.bAutopilotIgnoreLanes);
    stale = WarPowerStaleControl();
    ClearOrder(stale);
    TestFalse(TEXT("stale flag, clear: cleared"), stale.bAutopilotIgnoreLanes);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarPowerOrderFieldsTest, "SOLTest.WarPowerMove.OrderFieldsAsToday",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The order writers keep today's order fields (no behaviour change besides the flag): a body order with the unit
// arrival side (+Z for a degenerate one) and no point flag; a waypoint with its offset and the pass speed and capture
// radius floored at 0; a clear with no body and no point flag
bool FSOLWarPowerOrderFieldsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLAutopilotOrder;

    FSOLShipControlFragment body = WarPowerStaleControl();
    WriteBodyOrder(body, 3, FVector3d(3.0, 4.0, 0.0), true);
    TestEqual(TEXT("body: index"), body.AutopilotBodyIndex, 3);
    TestTrue(TEXT("body: unit arrival side"), body.AutopilotOffsetDir.Equals(FVector3d(0.6, 0.8, 0.0), 1.0e-12));
    TestFalse(TEXT("body: not a point order"), body.bAutopilotPoint);
    WriteBodyOrder(body, 2, FVector3d::ZeroVector, false);
    TestEqual(TEXT("degenerate side: index"), body.AutopilotBodyIndex, 2);
    TestTrue(TEXT("degenerate side: +Z"), body.AutopilotOffsetDir.Equals(FVector3d(0.0, 0.0, 1.0), 0.0));

    FSOLShipControlFragment point = WarPowerStaleControl();
    WritePointOrder(point, 9, FVector3d(1.0e6, -2.0e6, 3.0e5), 2500.0, 5.0e5);
    TestEqual(TEXT("point: index"), point.AutopilotBodyIndex, 9);
    TestTrue(TEXT("point: offset"), point.AutopilotPointOffsetM.Equals(FVector3d(1.0e6, -2.0e6, 3.0e5), 0.0));
    TestEqual(TEXT("point: pass speed"), point.AutopilotPassSpeedMps, 2500.0);
    TestEqual(TEXT("point: capture radius"), point.AutopilotCaptureRadiusM, 5.0e5);
    TestTrue(TEXT("point: a point order"), point.bAutopilotPoint);
    WritePointOrder(point, 9, FVector3d::ZeroVector, -5.0, -1.0);
    TestEqual(TEXT("point: negative pass speed floored"), point.AutopilotPassSpeedMps, 0.0);
    TestEqual(TEXT("point: negative capture radius floored"), point.AutopilotCaptureRadiusM, 0.0);

    FSOLShipControlFragment cleared = WarPowerStaleControl();
    ClearOrder(cleared);
    TestEqual(TEXT("clear: no body"), cleared.AutopilotBodyIndex, static_cast<int32>(INDEX_NONE));
    TestFalse(TEXT("clear: not a point order"), cleared.bAutopilotPoint);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarPowerLimitTest, "SOLTest.WarPowerMove.MaxThrustLimit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// D22 "maximum thrust, 100 % effort": the War power limit is the envelope's full forward thrust (boost when on, the
// total cap applied), above the autopilot's comfortable limit; a lower total cap bounds it
bool FSOLWarPowerLimitTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    const FSOLGMeterState fullMeter;
    const double comfortMps2 = SOLNewtonian::AutopilotAccelLimitMps2(params.PilotLimits, fullMeter);
    for (const bool bBoost : { false, true })
    {
        const FString label = bBoost ? TEXT("boost") : TEXT("no boost");
        const double limitMps2 = SOLFlight::WarPowerAccelLimitMps2(params.Envelope, bBoost);
        const double fullMps2 = SOLNewtonian::LocalThrustAccelMps2(FVector3d(1.0, 0.0, 0.0), params.Envelope,
            bBoost).X;
        TestEqual(label + TEXT(": the full forward thrust (100 % effort)"), limitMps2, fullMps2, 1.0e-12);
        TestTrue(FString::Printf(TEXT("%s: %.4f above the autopilot limit %.4f"), *label, limitMps2, comfortMps2),
            limitMps2 > comfortMps2);
    }
    TestEqual(TEXT("no boost: 3 G forward"), SOLFlight::WarPowerAccelLimitMps2(params.Envelope, false),
        params.Envelope.ForwardAccelMps2, 1.0e-12);
    TestTrue(TEXT("boost raises it"), SOLFlight::WarPowerAccelLimitMps2(params.Envelope, true)
        > SOLFlight::WarPowerAccelLimitMps2(params.Envelope, false));

    // A total cap below the forward thrust bounds the limit
    FSOLThrustEnvelope capped = params.Envelope;
    capped.MaxTotalAccelMps2 = 2.0 * SOLNewtonian::STANDARD_GRAVITY_MPS2;
    TestEqual(TEXT("capped at 2 G"), SOLFlight::WarPowerAccelLimitMps2(capped, true), capped.MaxTotalAccelMps2,
        1.0e-12);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarPowerStepTest, "SOLTest.WarPowerMove.PulseStep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// D22 and the pulse decision: with a full meter, accelerating toward a far target, the War power step thrusts at its
// pulse acceleration (within 1 %, never above it; the guidance's 0.9 margin is not taken off it): about 6.8 G with
// boost, the 3 G forward thrust without, harder than the normal step; with the meter at the War power floor (below the
// band) it recovers at most at the sustained 1.5 G and its pulse state says so
bool FSOLWarPowerStepTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    const FSOLGMeterState fullMeter;
    const double g0 = SOLNewtonian::STANDARD_GRAVITY_MPS2;
    const double comfortMps2 = SOLNewtonian::AutopilotAccelLimitMps2(params.PilotLimits, fullMeter);
    FSOLWarPowerPulseState unused;
    const FWarPowerRun normal = WarPowerFarStep(false, false, fullMeter, unused);
    const double normalMps2 = normal.Step.ThrustAccelWorldMps2.Size();
    TestTrue(FString::Printf(TEXT("normal: %.6f within the autopilot limit %.6f"), normalMps2, comfortMps2),
        normalMps2 <= comfortMps2 * (1.0 + 1.0e-9) && normalMps2 > 0.0);

    // Full meter: the pulse limit
    for (const bool bBoost : { false, true })
    {
        const FString label = bBoost ? TEXT("War power, boost") : TEXT("War power");
        FSOLWarPowerPulseState probe;
        const double limitMps2 = SOLFlight::WarPowerPulseAccelMps2(params.Envelope, params.PilotLimits, fullMeter,
            bBoost, 1.0, probe);
        const double expectedMps2 = FMath::Min(SOL::WAR_POWER_PULSE_G * g0,
            SOLFlight::WarPowerAccelLimitMps2(params.Envelope, bBoost));
        TestEqual(label + TEXT(": a full meter pulses at min(pulse G, full thrust)"), limitMps2, expectedMps2,
            1.0e-9 * expectedMps2);
        FSOLWarPowerPulseState pulse;
        const FWarPowerRun war = WarPowerFarStep(true, bBoost, fullMeter, pulse);
        const double warMps2 = war.Step.ThrustAccelWorldMps2.Size();
        TestTrue(FString::Printf(TEXT("%s: %.6f at most the limit %.6f"), *label, warMps2, limitMps2),
            warMps2 <= limitMps2 * (1.0 + 1.0e-9));
        TestTrue(FString::Printf(TEXT("%s: %.6f at the limit (1 %%), not 0.9 of it"), *label, warMps2),
            warMps2 >= 0.99 * limitMps2);
        TestTrue(FString::Printf(TEXT("%s: %.6f harder than the normal %.6f"), *label, warMps2, normalMps2),
            warMps2 > normalMps2);
        TestTrue(label + TEXT(": along the nose toward the target"), war.Step.ThrustAccelWorldMps2.X > 0.0);
        TestTrue(label + TEXT(": accelerating"), war.Guidance.Phase == ESOLGuidancePhase::Accelerate);
        TestFalse(label + TEXT(": still pulsing"), pulse.bRecovering);
    }

    // A drained meter: recover at the sustained limit, boost or not
    for (const double level : { SOL::WAR_POWER_METER_FLOOR, 0.5 * SOL::PILOT_CRITICAL_METER })
    {
        for (const bool bBoost : { false, true })
        {
            const FString label = FString::Printf(TEXT("meter %.3f%s"), level, bBoost ? TEXT(", boost") : TEXT(""));
            FSOLWarPowerPulseState pulse;
            const FWarPowerRun war = WarPowerFarStep(true, bBoost, WarPowerMeter(level), pulse);
            const double warMps2 = war.Step.ThrustAccelWorldMps2.Size();
            TestTrue(FString::Printf(TEXT("%s: %.6f at most the sustained 1.5 G"), *label, warMps2),
                warMps2 <= SOL::PILOT_SUSTAINED_G * g0 * (1.0 + 1.0e-9));
            TestTrue(label + TEXT(": still thrusts"), warMps2 > 0.0);
            TestTrue(label + TEXT(": recovering"), pulse.bRecovering);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarPowerMeterFloorTest, "SOLTest.WarPowerMove.MeterFloor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The user's G decision: the War power meter floor sits slightly above the critical level (0.25), never at or below it
bool FSOLWarPowerMeterFloorTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("the critical level is 0.25"), SOL::PILOT_CRITICAL_METER, 0.25);
    TestTrue(FString::Printf(TEXT("floor %.4f above the critical level"), SOL::WAR_POWER_METER_FLOOR),
        SOL::WAR_POWER_METER_FLOOR > SOL::PILOT_CRITICAL_METER);
    TestTrue(FString::Printf(TEXT("floor %.4f only slightly above it (at most 0.35)"), SOL::WAR_POWER_METER_FLOOR),
        SOL::WAR_POWER_METER_FLOOR <= SOL::PILOT_CRITICAL_METER + 0.1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarPowerPulseConstantsTest, "SOLTest.WarPowerMove.PulseConstants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The pulse decision's numbers: the pulse level x = sqrt(0.5) of the way from sustained to the 9 G maximum (about
// 6.8 G) drains the meter as fast as 1.5 G refills it (0.05 / s), so the pulse runs half the time and averages about
// 4.15 G; the band is 0.4 (the grey-out threshold) to 0.6, above the hard floor 0.3; boost reaches the pulse level,
// the plain forward thrust does not
bool FSOLWarPowerPulseConstantsTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    const FSOLPilotGLimits& limits = params.PilotLimits;
    const double g0 = SOLNewtonian::STANDARD_GRAVITY_MPS2;
    const double expectedG = limits.SustainedG + (limits.MaxG - limits.SustainedG) * FMath::Sqrt(0.5);
    TestTrue(FString::Printf(TEXT("pulse %.4f G = sustained + (max - sustained) sqrt(0.5) = %.4f G"),
        SOL::WAR_POWER_PULSE_G, expectedG), FMath::Abs(SOL::WAR_POWER_PULSE_G - expectedG) <= 0.01);
    const double drainPerS = SOLNewtonian::GDrainPerS(SOL::WAR_POWER_PULSE_G, limits);
    TestTrue(FString::Printf(TEXT("drain %.5f / s = the recovery %.5f / s"), drainPerS, limits.RecoverPerS),
        FMath::Abs(drainPerS - limits.RecoverPerS) <= 0.02 * limits.RecoverPerS);
    const double duty = limits.RecoverPerS / (drainPerS + limits.RecoverPerS);
    TestTrue(FString::Printf(TEXT("duty %.4f, half the time"), duty), FMath::Abs(duty - 0.5) <= 0.01);
    const double averageG = duty * SOL::WAR_POWER_PULSE_G + (1.0 - duty) * limits.SustainedG;
    TestTrue(FString::Printf(TEXT("average %.4f G, about 4.15"), averageG), FMath::Abs(averageG - 4.15) <= 0.03);

    TestEqual(TEXT("band low = the grey-out threshold"), SOL::WAR_POWER_BAND_LOW_METER, SOL::PILOT_VISION_START_METER);
    TestEqual(TEXT("band high 0.6"), SOL::WAR_POWER_BAND_HIGH_METER, 0.6);
    TestTrue(TEXT("band above the hard floor"), SOL::WAR_POWER_BAND_LOW_METER > SOL::WAR_POWER_METER_FLOOR
        && SOL::WAR_POWER_BAND_HIGH_METER > SOL::WAR_POWER_BAND_LOW_METER);
    TestEqual(TEXT("the hard floor stays 0.3"), SOL::WAR_POWER_METER_FLOOR, 0.3);
    TestTrue(TEXT("boost reaches the pulse level"), SOLFlight::WarPowerAccelLimitMps2(params.Envelope, true)
        >= SOL::WAR_POWER_PULSE_G * g0);
    TestTrue(TEXT("the plain forward thrust does not"), SOLFlight::WarPowerAccelLimitMps2(params.Envelope, false)
        < SOL::WAR_POWER_PULSE_G * g0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarPowerPulseRulesTest, "SOLTest.WarPowerMove.PulseRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The pulse phase: pulsing at min(pulse G, full thrust) until the meter reaches the band's low edge, then recovering at
// the sustained 1.5 G until it is back at the high edge (hysteresis, a transient flag); a pulse on a long substep is
// cut so the meter ends at or above the low edge; in G-LOC no more than sustained; the pulse level is a parameter
bool FSOLWarPowerPulseRulesTest::RunTest(const FString& /*parameters*/)
{
    using SOLFlight::WarPowerPulseAccelMps2;
    const FSOLFlightParams params;
    const FSOLThrustEnvelope& env = params.Envelope;
    const FSOLPilotGLimits& limits = params.PilotLimits;
    const double g0 = SOLNewtonian::STANDARD_GRAVITY_MPS2;
    const double sustainedMps2 = SOL::PILOT_SUSTAINED_G * g0;
    const double pulseMps2 = FMath::Min(SOL::WAR_POWER_PULSE_G * g0, SOLFlight::WarPowerAccelLimitMps2(env, true));

    // A fresh order on a full meter pulses
    FSOLWarPowerPulseState fresh;
    TestEqual(TEXT("full meter, boost: the pulse"), WarPowerPulseAccelMps2(env, limits, FSOLGMeterState(), true, 0.1,
        fresh), pulseMps2, 1.0e-9 * pulseMps2);
    TestFalse(TEXT("full meter: pulsing"), fresh.bRecovering);
    FSOLWarPowerPulseState unboosted;
    TestEqual(TEXT("full meter, no boost: the 3 G forward thrust"), WarPowerPulseAccelMps2(env, limits,
        FSOLGMeterState(), false, 0.1, unboosted), SOLFlight::WarPowerAccelLimitMps2(env, false), 1.0e-9);

    // In the band: a pulse keeps pulsing, a recovery keeps recovering
    FSOLWarPowerPulseState pulsing;
    const double midPulseMps2 = WarPowerPulseAccelMps2(env, limits, WarPowerMeter(0.5), true, 0.1, pulsing);
    TestTrue(FString::Printf(TEXT("meter 0.5, pulsing: %.4f m/s^2 above sustained, at most the pulse"), midPulseMps2),
        midPulseMps2 > sustainedMps2 && midPulseMps2 <= pulseMps2 * (1.0 + 1.0e-9));
    TestFalse(TEXT("meter 0.5, pulsing: stays pulsing"), pulsing.bRecovering);
    FSOLWarPowerPulseState recovering;
    recovering.bRecovering = true;
    TestEqual(TEXT("meter 0.5, recovering: sustained"), WarPowerPulseAccelMps2(env, limits, WarPowerMeter(0.5), true,
        0.1, recovering), sustainedMps2, 1.0e-9 * sustainedMps2);
    TestTrue(TEXT("meter 0.5, recovering: stays recovering"), recovering.bRecovering);

    // The band's edges switch the phase
    for (const double level : { SOL::WAR_POWER_BAND_LOW_METER, SOL::WAR_POWER_BAND_LOW_METER - 0.01 })
    {
        FSOLWarPowerPulseState low;
        TestEqual(FString::Printf(TEXT("meter %.3f, pulsing: recover at sustained"), level), WarPowerPulseAccelMps2(env,
            limits, WarPowerMeter(level), true, 0.1, low), sustainedMps2, 1.0e-9 * sustainedMps2);
        TestTrue(FString::Printf(TEXT("meter %.3f: recovering"), level), low.bRecovering);
    }
    FSOLWarPowerPulseState high;
    high.bRecovering = true;
    TestTrue(TEXT("meter 0.6, recovering: pulses again"), WarPowerPulseAccelMps2(env, limits,
        WarPowerMeter(SOL::WAR_POWER_BAND_HIGH_METER), true, 0.1, high) > sustainedMps2);
    TestFalse(TEXT("meter 0.6: pulsing"), high.bRecovering);

    // Long substeps: the pulse is cut so the meter ends at or above the low edge
    for (const double dt : { 10.0, 30.0, 169.0 })
    {
        for (const double level : { 0.45, 0.6, 1.0 })
        {
            FSOLWarPowerPulseState cut;
            const FSOLGMeterState meter = WarPowerMeter(level);
            const double limitMps2 = WarPowerPulseAccelMps2(env, limits, meter, true, dt, cut);
            const double afterMeter = SOLNewtonian::StepGMeter(meter, limitMps2 / g0, limits, dt).Meter;
            TestTrue(FString::Printf(TEXT("dt %.0f s, meter %.2f: %.4f G leaves %.6f, at or above the low edge"), dt,
                level, limitMps2 / g0, afterMeter), afterMeter >= SOL::WAR_POWER_BAND_LOW_METER - 1.0e-9);
            TestTrue(FString::Printf(TEXT("dt %.0f s, meter %.2f: at least sustained"), dt, level), limitMps2
                >= sustainedMps2 * (1.0 - 1.0e-9));
        }
    }

    // G-LOC: no more than sustained
    FSOLGMeterState out = WarPowerMeter(0.1);
    out.bBlackout = true;
    out.BlackoutRemainingS = 3.0;
    FSOLWarPowerPulseState blackout;
    TestTrue(TEXT("G-LOC: at most sustained"), WarPowerPulseAccelMps2(env, limits, out, true, 0.1, blackout)
        <= sustainedMps2 * (1.0 + 1.0e-9));

    // The pulse level is a parameter: 9 G with boost on a full meter
    FSOLWarPowerPulseState nine;
    TestEqual(TEXT("pulse level 9 G"), WarPowerPulseAccelMps2(env, limits, FSOLGMeterState(), true, 0.1, nine, 9.0),
        FMath::Min(9.0 * g0, SOLFlight::WarPowerAccelLimitMps2(env, true)), 1.0e-9 * 9.0 * g0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarPowerPulseBandTest, "SOLTest.WarPowerMove.PulseKeepsMeterBand",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Flown through the real War power step for 30 minutes of accelerating, the meter stepped from the achieved thrust: at
// every substep length (0.1 s to 169 s) the meter never drops below the hard floor, no G-LOC and no critical warning;
// up to 10 s substeps it stays at or above the 0.4 grey-out threshold after the startup burst; at 0.1 s it stays in
// the 0.4 - 0.6 band (one substep of recovery over the top allowed) and averages about 4.15 G, pulsing about half the
// time
bool FSOLWarPowerPulseBandTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPilotGLimits limits;
    for (const double dt : { 0.1, 1.0, 10.0, 30.0, 169.0 })
    {
        const FString label = FString::Printf(TEXT("dt %.1f s"), dt);
        const FWarPowerBurn burn = WarPowerBurnRun(dt, 1800.0);
        TestTrue(FString::Printf(TEXT("%s: lowest meter %.6f at or above the hard floor"), *label, burn.MinMeter),
            burn.MinMeter >= SOL::WAR_POWER_METER_FLOOR - 1.0e-9);
        TestFalse(label + TEXT(": no G-LOC"), burn.bBlackout);
        TestFalse(label + TEXT(": no critical warning"), burn.bCritical);
        if (dt <= 10.0)
        {
            TestTrue(FString::Printf(TEXT("%s: after the startup burst the meter %.6f stays at or above 0.4"), *label,
                burn.MinMeterAfter60), burn.MinMeterAfter60 >= SOL::WAR_POWER_BAND_LOW_METER - 1.0e-9);
        }
        if (dt <= 0.1)
        {
            TestTrue(FString::Printf(TEXT("%s: highest meter %.6f in the band"), *label, burn.MaxMeterAfter60),
                burn.MaxMeterAfter60 <= SOL::WAR_POWER_BAND_HIGH_METER + limits.RecoverPerS * dt + 1.0e-9);
            TestTrue(FString::Printf(TEXT("%s: average %.4f G, about 4.15"), *label, burn.AverageG),
                FMath::Abs(burn.AverageG - 4.15) <= 0.03 * 4.15);
            TestTrue(FString::Printf(TEXT("%s: pulsing %.3f of the time, about half"), *label, burn.PulseFraction),
                FMath::Abs(burn.PulseFraction - 0.5) <= 0.06);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarPowerPulseLevelTest, "SOLTest.WarPowerMove.PulseLevelBeatsNineG",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The pulse level choice: pulsing at about 6.8 G against the 1.5 G recovery averages about 4.15 G; pulsing at the full
// 9 G drains twice as fast as it refills, runs a third of the time and averages about 4.0 G, so 6.8 G is faster
bool FSOLWarPowerPulseLevelTest::RunTest(const FString& /*parameters*/)
{
    const double sixEightG = WarPowerMeanPulseG(SOL::WAR_POWER_PULSE_G, 0.1, 1800.0);
    const double nineG = WarPowerMeanPulseG(9.0, 0.1, 1800.0);
    TestTrue(FString::Printf(TEXT("6.8 G pulses average %.4f G, about 4.15"), sixEightG),
        FMath::Abs(sixEightG - 4.15) <= 0.03 * 4.15);
    TestTrue(FString::Printf(TEXT("9 G pulses average %.4f G, about 4.0"), nineG), FMath::Abs(nineG - 4.0)
        <= 0.03 * 4.0);
    TestTrue(FString::Printf(TEXT("6.8 G (%.4f) beats 9 G (%.4f)"), sixEightG, nineG), sixEightG > nineG);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarPowerTripTest, "SOLTest.WarPowerMove.PulseTripEarthMars",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The trip the pulse buys (2.25e11 m flip and burn from rest, braking on the normal plan), flown through the real
// steps: plain AUTO about 72.4 h; War power about 59 h at 1 s and 10 s substeps (it arrives, without overshoot), the
// gain falling on long warp substeps (loose bands: about 61 h at 30 s, 66 h at 169 s), never slower than plain AUTO;
// at every substep length the meter stays at or above the hard floor and no G-LOC
bool FSOLWarPowerTripTest::RunTest(const FString& /*parameters*/)
{
    const double maxS = 1.5 * 72.4 * WARPOWER_HOUR_S;
    const FWarPowerTrip plain = WarPowerFly(false, WARPOWER_TRIP_M, 10.0, maxS);
    TestTrue(TEXT("plain AUTO: reaches the target"), plain.CloseS > 0.0);
    TestTrue(FString::Printf(TEXT("plain AUTO %.2f h, about 72.4"), plain.CloseS / WARPOWER_HOUR_S),
        FMath::Abs(plain.CloseS / WARPOWER_HOUR_S - 72.4) <= 0.03 * 72.4);

    struct FTripCase
    {
        double Dt;
        double ExpectedH;
        double Tolerance;
        bool bMustArrive;
    };
    const FTripCase cases[] = { { 1.0, 59.0, 0.03, true }, { 10.0, 59.0, 0.03, true }, { 30.0, 61.4, 0.07, false },
        { 169.0, 65.7, 0.07, false } };
    double previousS = 0.0;
    for (const FTripCase& trip : cases)
    {
        const FString label = FString::Printf(TEXT("War power, dt %.0f s"), trip.Dt);
        const FWarPowerTrip war = WarPowerFly(true, WARPOWER_TRIP_M, trip.Dt, maxS);
        const double hours = war.CloseS / WARPOWER_HOUR_S;
        TestTrue(label + TEXT(": reaches the target"), war.CloseS > 0.0);
        TestTrue(FString::Printf(TEXT("%s: %.2f h, about %.1f"), *label, hours, trip.ExpectedH),
            FMath::Abs(hours - trip.ExpectedH) <= trip.Tolerance * trip.ExpectedH);
        TestTrue(FString::Printf(TEXT("%s: %.2f h never slower than plain AUTO %.2f h"), *label, hours,
            plain.CloseS / WARPOWER_HOUR_S), war.CloseS > 0.0 && war.CloseS < plain.CloseS);
        TestTrue(FString::Printf(TEXT("%s: lowest meter %.6f at or above the hard floor"), *label, war.MinMeter),
            war.MinMeter >= SOL::WAR_POWER_METER_FLOOR - 1.0e-9);
        TestFalse(label + TEXT(": no G-LOC"), war.bBlackout);
        TestTrue(label + TEXT(": the gain does not grow with the substep"), war.CloseS >= 0.98 * previousS);
        previousS = war.CloseS;
        if (trip.bMustArrive)
        {
            TestTrue(label + TEXT(": arrives"), war.bArrived);
            TestTrue(FString::Printf(TEXT("%s: overshoot %.6g m"), *label, war.MaxOvershootM), war.MaxOvershootM
                <= 1.0e5);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarPowerGuidanceTest, "SOLTest.WarPowerMove.PulseBrakeAtAutopilotLimit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The War power plan accelerates at least at the margin-reduced plan of its pulse limit but brakes on the normal
// autopilot's plan (about 1.35 G), so a meter drained by the pulses can still stop in time (no overshoot); at a full
// meter with boost it plans harder than it brakes, and at any meter the brake budget is exactly AutopilotStep's
bool FSOLWarPowerGuidanceTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    const double gravityMps2 = 2.0e-3;
    for (const double level : { 1.0, 0.6, SOL::WAR_POWER_METER_FLOOR, 0.2 })
    {
        for (const bool bBoost : { true, false })
        {
            const FString label = FString::Printf(TEXT("meter %.3f, boost %d"), level, bBoost);
            const FSOLGMeterState meter = WarPowerMeter(level);
            FSOLWarPowerPulseState pulse;
            const double stepLimitMps2 = SOLFlight::WarPowerPulseAccelMps2(params.Envelope, params.PilotLimits, meter,
                bBoost, 1.0, pulse);
            const FSOLGuidanceParams war = SOLFlight::MakeWarPowerPulseGuidanceParams(params.Envelope,
                params.PilotLimits, meter, gravityMps2, bBoost, stepLimitMps2);
            const FSOLGuidanceParams plan = SOLNewtonian::MakeGuidanceParams(params.Envelope, stepLimitMps2,
                gravityMps2, true, bBoost);
            const FSOLGuidanceParams normal = SOLNewtonian::MakeGuidanceParams(params.Envelope,
                SOLNewtonian::AutopilotAccelLimitMps2(params.PilotLimits, meter), gravityMps2, true, false);
            TestTrue(FString::Printf(TEXT("%s: plans %.6f, at least the margin-reduced %.6f"), *label,
                war.PlanAccelMps2, plan.PlanAccelMps2), war.PlanAccelMps2 >= plan.PlanAccelMps2 * (1.0 - 1.0e-9));
            TestEqual(label + TEXT(": brakes on the normal autopilot budget"), war.BrakeAccelMps2,
                normal.BrakeAccelMps2, 1.0e-9);
            TestTrue(label + TEXT(": budgets positive and finite"), war.PlanAccelMps2 > 0.0
                && war.BrakeAccelMps2 > 0.0 && FMath::IsFinite(war.PlanAccelMps2));
        }
    }
    FSOLWarPowerPulseState fullPulse;
    const double fullLimitMps2 = SOLFlight::WarPowerPulseAccelMps2(params.Envelope, params.PilotLimits,
        FSOLGMeterState(), true, 1.0, fullPulse);
    const FSOLGuidanceParams fullBoost = SOLFlight::MakeWarPowerPulseGuidanceParams(params.Envelope,
        params.PilotLimits, FSOLGMeterState(), 0.0, true, fullLimitMps2);
    TestTrue(TEXT("full meter, boost: the plan accelerates harder than it brakes"), fullBoost.PlanAccelMps2
        > fullBoost.BrakeAccelMps2);
    const double brakeG = fullBoost.BrakeAccelMps2 / SOLNewtonian::STANDARD_GRAVITY_MPS2;
    TestTrue(FString::Printf(TEXT("full meter: brakes at %.4f G, about 1.35"), brakeG), FMath::Abs(brakeG - 1.35)
        <= 0.01 * 1.35);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarPowerPulseStateTest, "SOLTest.WarPowerMove.PulseStateNotSaved",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The pulse needs no persisted state: its phase is a plain transient field of the state fragment (not reflected),
// starting as a pulse; the save schema does not move
bool FSOLWarPowerPulseStateTest::RunTest(const FString& /*parameters*/)
{
    TestFalse(TEXT("a new pulse state pulses"), FSOLWarPowerPulseState().bRecovering);
    TestFalse(TEXT("a new state fragment's pulse state pulses"), FSOLShipStateFragment().WarPowerPulse.bRecovering);
    TestNull(TEXT("not a reflected property"), FindFProperty<FProperty>(FSOLShipStateFragment::StaticStruct(),
        TEXT("WarPowerPulse")));
    TestEqual(TEXT("SAVE_VERSION stays 13"), SOL::Settings::SAVE_VERSION, 13);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarPowerNoSameTargetBlockTest, "SOLTest.WarPowerMove.NoSameTargetBlock",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// User decision (2026-11-17): a running Move To to a body no longer greys out War power to the same body, nor the other
// way round; the new command replaces the old one. Neither row returns AlreadyMovingTo in any of the 512 contexts, and
// in a favourable context with the autopilot flying to the body both rows are available
bool FSOLWarPowerNoSameTargetBlockTest::RunTest(const FString& /*parameters*/)
{
    for (const ESOLObjectType type : WARPOWER_BODY_TYPES)
    {
        const FString name = SOLActionTable::TypeName(type);
        FSOLActionContext moving;
        moving.bHasShip = true;
        moving.bPilotHasControl = true;
        moving.bIsTargetable = true;
        moving.bIsMoveTarget = true;
        TestEqual(name + TEXT(": Move To while flying there"), static_cast<int32>(SOLActionTable::Evaluate(type,
            ESOLAction::MoveTo, moving)), static_cast<int32>(ESOLActionBlock::None));
        TestEqual(name + TEXT(": War power while flying there"), static_cast<int32>(SOLActionTable::Evaluate(type,
            ESOLAction::MoveToWarPower, moving)), static_cast<int32>(ESOLActionBlock::None));
        int32 sameTargetBlocks = 0;
        for (int32 mask = 0; mask < (1 << WARPOWER_CONTEXT_FLAGS); ++mask)
        {
            const FSOLActionContext context = WarPowerContextFromMask(mask);
            sameTargetBlocks += SOLActionTable::Evaluate(type, ESOLAction::MoveTo, context)
                == ESOLActionBlock::AlreadyMovingTo ? 1 : 0;
            sameTargetBlocks += SOLActionTable::Evaluate(type, ESOLAction::MoveToWarPower, context)
                == ESOLActionBlock::AlreadyMovingTo ? 1 : 0;
        }
        TestEqual(name + TEXT(": no same-target block in any context"), sameTargetBlocks, 0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLWarPowerHudTest, "SOLTest.WarPowerMove.HudBoostAndActivity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The HUD while a War power order flies: the boost indicator shows on (the order flies boosted) whatever the boost
// switch says, and the autopilot's activity line marks the order with a short upper-case token from SOLMenuText; a
// normal order shows neither
bool FSOLWarPowerHudTest::RunTest(const FString& /*parameters*/)
{
    using SOLAutopilotOrder::IsBoostShown;
    TestFalse(TEXT("switch off, normal order: boost off"), IsBoostShown(false, false));
    TestTrue(TEXT("switch on, normal order: boost on"), IsBoostShown(true, false));
    TestTrue(TEXT("switch off, War power: boost on"), IsBoostShown(false, true));
    TestTrue(TEXT("switch on, War power: boost on"), IsBoostShown(true, true));

    const FString token = SOLMenuText::AUTOPILOT_WAR_POWER_ACTIVITY;
    TestEqual(TEXT("War power order: the SOLMenuText token"), FString(SOLAutopilotOrder::AutopilotActivityText(true)),
        token);
    TestTrue(TEXT("normal order: no activity text"), FString(SOLAutopilotOrder::AutopilotActivityText(false))
        .IsEmpty());
    TestFalse(TEXT("token: not empty"), token.TrimStartAndEnd().IsEmpty());
    TestTrue(TEXT("token: short"), token.Len() <= 24);
    TestEqual(TEXT("token: upper case like the HUD tags"), token, token.ToUpper());
    TestFalse(TEXT("token: no placeholder"), token.Contains(TEXT("{")));
    TestNotEqual(TEXT("token: not the LANE tag"), token, FString(SOLMenuText::AUTOPILOT_LANE_TAG));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
