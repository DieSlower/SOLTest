/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Flight/SOLFlightMode.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLSettingsRules.h"
#include "Menu/SOLSettingsSave.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #19 (19a) flight modes, steering gate, boost toggle and key layout, written against the 19a
// contract in Docs/Plans/19-flight-modes-and-controls-plan.md before any production code existed. The AUTO input
// latch tests were rewritten against the per-channel latch contract (LatchedChannels, RelatchInput).

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // The manual-input threshold of the contract, restated so the tests stay independent
    constexpr double FMM_INPUT_THRESHOLD = 1.0e-3;

    // Settings schema version that introduced the issue #19 key layout
    constexpr int32 FMM_LAYOUT_VERSION = 3;

    // Settings schema version that introduced the issue #20 key layout (the current one)
    constexpr int32 FMM_ROUTE_LAYOUT_VERSION = 4;

    // Number of rebindable actions after issue #20 (32 after issue #19; issue #20 removes ToggleMap and adds OpenMap,
    // OpenMapJump, Resume and ToggleRouteOverlay)
    constexpr int32 FMM_BINDING_COUNT = 35;

    // The signed input channel bits of the contract, restated so the tests stay independent
    constexpr int32 FMM_CH_THRUST_FORWARD = 1 << 0;
    constexpr int32 FMM_CH_THRUST_BACK = 1 << 1;
    constexpr int32 FMM_CH_THRUST_RIGHT = 1 << 2;
    constexpr int32 FMM_CH_THRUST_LEFT = 1 << 3;
    constexpr int32 FMM_CH_THRUST_UP = 1 << 4;
    constexpr int32 FMM_CH_THRUST_DOWN = 1 << 5;
    constexpr int32 FMM_CH_ROLL_RIGHT = 1 << 6;
    constexpr int32 FMM_CH_ROLL_LEFT = 1 << 7;
    constexpr int32 FMM_CH_STEER_RIGHT = 1 << 8;
    constexpr int32 FMM_CH_STEER_LEFT = 1 << 9;
    constexpr int32 FMM_CH_STEER_UP = 1 << 10;
    constexpr int32 FMM_CH_STEER_DOWN = 1 << 11;
    constexpr int32 FMM_CH_STOP_SPIN = 1 << 12;

    // Every channel bit at once (13 consecutive bits)
    constexpr int32 FMM_CH_ALL = (1 << 13) - 1;

    //////////////////////////////////////////////////////////////////////////
    // Returns a pilot input with nothing pressed
    FSOLPilotInput FmmNoInput()
    {
        return FSOLPilotInput();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a pilot input with only the given thrust fractions
    FSOLPilotInput FmmThrust(const double x, const double y, const double z)
    {
        FSOLPilotInput input;
        input.Thrust = FVector3d(x, y, z);
        return input;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a pilot input with only the given roll
    FSOLPilotInput FmmRoll(const double roll)
    {
        FSOLPilotInput input;
        input.Roll = roll;
        return input;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a pilot input with only the given (gated) stick command
    FSOLPilotInput FmmSteer(const double yaw, const double pitch)
    {
        FSOLPilotInput input;
        input.Steer = FVector2d(yaw, pitch);
        return input;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a pilot input with only stop spin held
    FSOLPilotInput FmmStopSpin()
    {
        FSOLPilotInput input;
        input.bStopSpin = true;
        return input;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns two inputs held together (analog components summed, stop spin OR-ed)
    FSOLPilotInput FmmBoth(const FSOLPilotInput& a, const FSOLPilotInput& b)
    {
        FSOLPilotInput input;
        input.Thrust = a.Thrust + b.Thrust;
        input.Roll = a.Roll + b.Roll;
        input.Steer = a.Steer + b.Steer;
        input.bStopSpin = a.bStopSpin || b.bStopSpin;
        return input;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a quiet NaN for the invalid-input cases
    double FmmNaN()
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns positive infinity for the invalid-input cases
    double FmmInf()
    {
        return std::numeric_limits<double>::infinity();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the active-channel mask of an input as an int32 for the equality checks
    int32 FmmActive(const FSOLPilotInput& input)
    {
        return static_cast<int32>(SOLFlightMode::ActiveChannels(input));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the latched-channel mask of a mode state as an int32 for the equality checks
    int32 FmmLatched(const FSOLFlightModeState& state)
    {
        return static_cast<int32>(state.LatchedChannels);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a mode state already in Auto (engaged from Assisted) with the given input held at engage
    FSOLFlightModeState FmmAutoState(const FSOLPilotInput& heldInput)
    {
        FSOLFlightModeState state = SOLFlightMode::MakeInitialState(true);
        SOLFlightMode::EngageAuto(state, heldInput);
        return state;
    }

    // One full-deflection input that presses a single channel, and the channel bit it must set
    struct FFmmChannelCase
    {
        const TCHAR* Name;
        FSOLPilotInput Input;
        int32 Channel;
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns one case per channel, each pressing that channel alone
    TArray<FFmmChannelCase> FmmChannelCases()
    {
        return TArray<FFmmChannelCase>
        {
            { TEXT("thrust forward"), FmmThrust(1.0, 0.0, 0.0), FMM_CH_THRUST_FORWARD },
            { TEXT("thrust back"), FmmThrust(-1.0, 0.0, 0.0), FMM_CH_THRUST_BACK },
            { TEXT("strafe right"), FmmThrust(0.0, 1.0, 0.0), FMM_CH_THRUST_RIGHT },
            { TEXT("strafe left"), FmmThrust(0.0, -1.0, 0.0), FMM_CH_THRUST_LEFT },
            { TEXT("thrust up"), FmmThrust(0.0, 0.0, 1.0), FMM_CH_THRUST_UP },
            { TEXT("thrust down"), FmmThrust(0.0, 0.0, -1.0), FMM_CH_THRUST_DOWN },
            { TEXT("roll right"), FmmRoll(1.0), FMM_CH_ROLL_RIGHT },
            { TEXT("roll left"), FmmRoll(-1.0), FMM_CH_ROLL_LEFT },
            { TEXT("steer right"), FmmSteer(0.3, 0.0), FMM_CH_STEER_RIGHT },
            { TEXT("steer left"), FmmSteer(-0.3, 0.0), FMM_CH_STEER_LEFT },
            { TEXT("steer up"), FmmSteer(0.0, 0.3), FMM_CH_STEER_UP },
            { TEXT("steer down"), FmmSteer(0.0, -0.3), FMM_CH_STEER_DOWN },
            { TEXT("stop spin"), FmmStopSpin(), FMM_CH_STOP_SPIN },
        };
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a binding index by id (INDEX_NONE if the binding does not exist)
    int32 FmmBinding(const TCHAR* id)
    {
        return SOLKeyBindings::FindById(id);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two override maps hold exactly the same entries (ids case-insensitive, keys case-sensitive)
    bool FmmSameOverrides(const TMap<FString, FString>& a, const TMap<FString, FString>& b)
    {
        if (a.Num() != b.Num())
        {
            return false;
        }
        for (const TPair<FString, FString>& entry : a)
        {
            const FString* other = b.Find(entry.Key);
            if (other == nullptr || !other->Equals(entry.Value, ESearchCase::CaseSensitive))
            {
                return false;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the resolved key of a binding by id (empty if the binding does not exist)
    FString FmmKeyOf(const TArray<FString>& keys, const TCHAR* id)
    {
        const int32 binding = FmmBinding(id);
        return keys.IsValidIndex(binding) ? keys[binding] : FString();
    }
}

// ================================================================================================================
// Flight mode state machine
// ================================================================================================================

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathInitialStateTest, "SOLTest.FlightModeMath.InitialState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The start mode follows the launch setting; a default state is Assisted; nothing starts latched or in Auto
bool FSOLFlightModeMathInitialStateTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    const FSOLFlightModeState assisted = MakeInitialState(true);
    TestTrue(TEXT("assist on -> Assisted"), assisted.Mode == ESOLFlightMode::Assisted);
    TestEqual(TEXT("assisted: nothing latched"), FmmLatched(assisted), 0);
    TestFalse(TEXT("assisted is not auto"), IsAuto(assisted));

    const FSOLFlightModeState manual = MakeInitialState(false);
    TestTrue(TEXT("assist off -> Manual"), manual.Mode == ESOLFlightMode::Manual);
    TestEqual(TEXT("manual: nothing latched"), FmmLatched(manual), 0);
    TestFalse(TEXT("manual is not auto"), IsAuto(manual));

    const FSOLFlightModeState defaulted;
    TestTrue(TEXT("default-constructed state is Assisted"), defaulted.Mode == ESOLFlightMode::Assisted);
    TestEqual(TEXT("default-constructed state: nothing latched"), FmmLatched(defaulted), 0);

    const FSOLFlightModeState automatic = FmmAutoState(FmmNoInput());
    TestTrue(TEXT("IsAuto after engage"), IsAuto(automatic));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathFlightAssistMappingTest,
    "SOLTest.FlightModeMath.FlightAssistMapping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The processor's assist flag: off only in Manual (Auto falls back to assisted flight, never an uncommanded coast)
bool FSOLFlightModeMathFlightAssistMappingTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    TestFalse(TEXT("Manual -> assist off"), FlightAssistFor(ESOLFlightMode::Manual));
    TestTrue(TEXT("Assisted -> assist on"), FlightAssistFor(ESOLFlightMode::Assisted));
    TestTrue(TEXT("Auto -> assist on"), FlightAssistFor(ESOLFlightMode::Auto));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathToggleAssistCycleTest, "SOLTest.FlightModeMath.ToggleAssistCycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// C flips Manual and Assisted; in Auto it cancels to Assisted and clears every latched channel
bool FSOLFlightModeMathToggleAssistCycleTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    FSOLFlightModeState state = MakeInitialState(false);
    TestTrue(TEXT("Manual -> Assisted returns true"), ToggleAssist(state, true));
    TestTrue(TEXT("now Assisted"), state.Mode == ESOLFlightMode::Assisted);
    TestTrue(TEXT("Assisted -> Manual returns true"), ToggleAssist(state, true));
    TestTrue(TEXT("now Manual"), state.Mode == ESOLFlightMode::Manual);
    TestTrue(TEXT("and back to Assisted"), ToggleAssist(state, true));
    TestTrue(TEXT("Assisted again"), state.Mode == ESOLFlightMode::Assisted);
    TestEqual(TEXT("toggling outside Auto latches nothing"), FmmLatched(state), 0);

    // Auto engaged from Manual with W and D held: C lands in Assisted, not in the pre-auto Manual
    FSOLFlightModeState automatic = MakeInitialState(false);
    EngageAuto(automatic, FmmThrust(1.0, 1.0, 0.0));
    TestEqual(TEXT("W and D latched before C"), FmmLatched(automatic), FMM_CH_THRUST_FORWARD | FMM_CH_THRUST_RIGHT);
    TestTrue(TEXT("Auto -> Assisted returns true"), ToggleAssist(automatic, true));
    TestTrue(TEXT("Auto -> Assisted"), automatic.Mode == ESOLFlightMode::Assisted);
    TestEqual(TEXT("latch cleared by C"), FmmLatched(automatic), 0);
    TestFalse(TEXT("no longer auto"), IsAuto(automatic));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathToggleAssistNoControlTest,
    "SOLTest.FlightModeMath.ToggleAssistIgnoredWithoutControl",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// During G-LOC the C toggle does nothing in any mode (the pilot is unconscious), and an Auto latch is kept
bool FSOLFlightModeMathToggleAssistNoControlTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    FSOLFlightModeState manual = MakeInitialState(false);
    TestFalse(TEXT("Manual: refused"), ToggleAssist(manual, false));
    TestTrue(TEXT("Manual kept"), manual.Mode == ESOLFlightMode::Manual);

    FSOLFlightModeState assisted = MakeInitialState(true);
    TestFalse(TEXT("Assisted: refused"), ToggleAssist(assisted, false));
    TestTrue(TEXT("Assisted kept"), assisted.Mode == ESOLFlightMode::Assisted);

    FSOLFlightModeState automatic = FmmAutoState(FmmRoll(1.0));
    TestFalse(TEXT("Auto: refused"), ToggleAssist(automatic, false));
    TestTrue(TEXT("Auto kept"), automatic.Mode == ESOLFlightMode::Auto);
    TestEqual(TEXT("roll latch kept"), FmmLatched(automatic), FMM_CH_ROLL_RIGHT);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathEngageFromEveryModeTest,
    "SOLTest.FlightModeMath.EngageFromEveryMode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Engaging from Manual or Assisted enters Auto and latches exactly the channels held at that moment
bool FSOLFlightModeMathEngageFromEveryModeTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    FSOLFlightModeState manual = MakeInitialState(false);
    TestTrue(TEXT("Manual -> Auto returns true"), EngageAuto(manual, FmmNoInput()));
    TestTrue(TEXT("Manual -> Auto"), manual.Mode == ESOLFlightMode::Auto);
    TestEqual(TEXT("nothing held: nothing latched"), FmmLatched(manual), 0);

    FSOLFlightModeState assisted = MakeInitialState(true);
    TestTrue(TEXT("Assisted -> Auto returns true"), EngageAuto(assisted, FmmThrust(0.0, -1.0, 0.0)));
    TestTrue(TEXT("Assisted -> Auto"), assisted.Mode == ESOLFlightMode::Auto);
    TestEqual(TEXT("strafe left held: only that latched"), FmmLatched(assisted), FMM_CH_THRUST_LEFT);

    // Each single channel held latches exactly that channel
    for (const FFmmChannelCase& entry : FmmChannelCases())
    {
        FSOLFlightModeState state = MakeInitialState(true);
        EngageAuto(state, entry.Input);
        TestEqual(FString::Printf(TEXT("%s latches only itself"), entry.Name), FmmLatched(state), entry.Channel);
    }

    // Several held at once latch all of them and nothing else
    FSOLFlightModeState several = MakeInitialState(true);
    EngageAuto(several, FmmBoth(FmmThrust(1.0, 1.0, 0.0), FmmStopSpin()));
    TestEqual(TEXT("W + D + stop spin latched"), FmmLatched(several),
        FMM_CH_THRUST_FORWARD | FMM_CH_THRUST_RIGHT | FMM_CH_STOP_SPIN);

    // Sub-threshold noise latches nothing; a real component beside noise latches only itself
    FSOLFlightModeState noisy = MakeInitialState(true);
    EngageAuto(noisy, FmmSteer(0.5 * FMM_INPUT_THRESHOLD, -0.5 * FMM_INPUT_THRESHOLD));
    TestEqual(TEXT("noise latches nothing"), FmmLatched(noisy), 0);
    FSOLFlightModeState noiseAndRoll = MakeInitialState(true);
    EngageAuto(noiseAndRoll, FmmBoth(FmmSteer(0.5 * FMM_INPUT_THRESHOLD, 0.0), FmmRoll(-1.0)));
    TestEqual(TEXT("roll beside noise: only roll left"), FmmLatched(noiseAndRoll), FMM_CH_ROLL_LEFT);

    // Engaging is an API call and works during G-LOC too (no control parameter)
    TestTrue(TEXT("engaged"), IsAuto(noisy));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathEngageWhileAutoTest,
    "SOLTest.FlightModeMath.EngageWhileAutoRearmsLatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A new order while in Auto keeps the mode (returns false) and replaces the latch with the channels held now
bool FSOLFlightModeMathEngageWhileAutoTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    FSOLFlightModeState state = FmmAutoState(FmmNoInput());
    TestEqual(TEXT("starts unlatched"), FmmLatched(state), 0);

    TestFalse(TEXT("re-engage returns false"), EngageAuto(state, FmmThrust(1.0, 0.0, 0.0)));
    TestTrue(TEXT("still Auto"), state.Mode == ESOLFlightMode::Auto);
    TestEqual(TEXT("latch armed by the held key"), FmmLatched(state), FMM_CH_THRUST_FORWARD);

    // Replaced, not added to: W is no longer held, so only roll left stays latched
    TestFalse(TEXT("re-engage with roll returns false"), EngageAuto(state, FmmRoll(-1.0)));
    TestEqual(TEXT("latch replaced by roll left"), FmmLatched(state), FMM_CH_ROLL_LEFT);

    TestFalse(TEXT("re-engage again returns false"), EngageAuto(state, FmmNoInput()));
    TestTrue(TEXT("still Auto again"), state.Mode == ESOLFlightMode::Auto);
    TestEqual(TEXT("latch cleared: nothing held now"), FmmLatched(state), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathDisengageAndArrivalTest,
    "SOLTest.FlightModeMath.DisengageAndArrival",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Disengage and arrival both hand back to Assisted with no latch (even when auto started from Manual); outside Auto
// they are no-ops
bool FSOLFlightModeMathDisengageAndArrivalTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    FSOLFlightModeState disengaged = MakeInitialState(false);
    EngageAuto(disengaged, FmmThrust(1.0, 1.0, 0.0));
    TestTrue(TEXT("disengage returns true"), DisengageAuto(disengaged));
    TestTrue(TEXT("disengage -> Assisted"), disengaged.Mode == ESOLFlightMode::Assisted);
    TestEqual(TEXT("disengage clears the latch"), FmmLatched(disengaged), 0);
    TestFalse(TEXT("second disengage returns false"), DisengageAuto(disengaged));
    TestTrue(TEXT("still Assisted"), disengaged.Mode == ESOLFlightMode::Assisted);

    FSOLFlightModeState arrived = MakeInitialState(false);
    EngageAuto(arrived, FmmBoth(FmmRoll(1.0), FmmStopSpin()));
    TestTrue(TEXT("arrival returns true"), NotifyAutopilotArrived(arrived));
    TestTrue(TEXT("arrival -> Assisted"), arrived.Mode == ESOLFlightMode::Assisted);
    TestEqual(TEXT("arrival clears the latch"), FmmLatched(arrived), 0);
    TestFalse(TEXT("second arrival returns false"), NotifyAutopilotArrived(arrived));

    // Outside Auto nothing changes: Manual stays Manual
    FSOLFlightModeState manual = MakeInitialState(false);
    TestFalse(TEXT("disengage in Manual"), DisengageAuto(manual));
    TestTrue(TEXT("Manual kept by disengage"), manual.Mode == ESOLFlightMode::Manual);
    TestFalse(TEXT("arrival in Manual"), NotifyAutopilotArrived(manual));
    TestTrue(TEXT("Manual kept by arrival"), manual.Mode == ESOLFlightMode::Manual);
    return true;
}

// ================================================================================================================
// Input channels and manual input
// ================================================================================================================

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathChannelBitsTest, "SOLTest.FlightModeMath.ChannelBits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The 13 channel flags have the contract's bit values: distinct single bits filling bits 0..12
bool FSOLFlightModeMathChannelBitsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    TestEqual(TEXT("thrust forward"), static_cast<int32>(CHANNEL_THRUST_FORWARD), FMM_CH_THRUST_FORWARD);
    TestEqual(TEXT("thrust back"), static_cast<int32>(CHANNEL_THRUST_BACK), FMM_CH_THRUST_BACK);
    TestEqual(TEXT("thrust right"), static_cast<int32>(CHANNEL_THRUST_RIGHT), FMM_CH_THRUST_RIGHT);
    TestEqual(TEXT("thrust left"), static_cast<int32>(CHANNEL_THRUST_LEFT), FMM_CH_THRUST_LEFT);
    TestEqual(TEXT("thrust up"), static_cast<int32>(CHANNEL_THRUST_UP), FMM_CH_THRUST_UP);
    TestEqual(TEXT("thrust down"), static_cast<int32>(CHANNEL_THRUST_DOWN), FMM_CH_THRUST_DOWN);
    TestEqual(TEXT("roll right"), static_cast<int32>(CHANNEL_ROLL_RIGHT), FMM_CH_ROLL_RIGHT);
    TestEqual(TEXT("roll left"), static_cast<int32>(CHANNEL_ROLL_LEFT), FMM_CH_ROLL_LEFT);
    TestEqual(TEXT("steer right"), static_cast<int32>(CHANNEL_STEER_RIGHT), FMM_CH_STEER_RIGHT);
    TestEqual(TEXT("steer left"), static_cast<int32>(CHANNEL_STEER_LEFT), FMM_CH_STEER_LEFT);
    TestEqual(TEXT("steer up"), static_cast<int32>(CHANNEL_STEER_UP), FMM_CH_STEER_UP);
    TestEqual(TEXT("steer down"), static_cast<int32>(CHANNEL_STEER_DOWN), FMM_CH_STEER_DOWN);
    TestEqual(TEXT("stop spin"), static_cast<int32>(CHANNEL_STOP_SPIN), FMM_CH_STOP_SPIN);

    // The case table covers every bit exactly once
    int32 combined = 0;
    for (const FFmmChannelCase& entry : FmmChannelCases())
    {
        TestEqual(FString::Printf(TEXT("%s bit not seen before"), entry.Name), combined & entry.Channel, 0);
        combined |= entry.Channel;
    }
    TestEqual(TEXT("13 bits, all distinct"), combined, FMM_CH_ALL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathActiveChannelsTest, "SOLTest.FlightModeMath.ActiveChannels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Each component sets the channel of its sign only when strictly beyond the threshold; combinations set each bit
bool FSOLFlightModeMathActiveChannelsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    TestEqual(TEXT("nothing pressed"), FmmActive(FmmNoInput()), 0);

    // Each channel alone at full deflection
    for (const FFmmChannelCase& entry : FmmChannelCases())
    {
        TestEqual(FString::Printf(TEXT("%s alone"), entry.Name), FmmActive(entry.Input), entry.Channel);
    }

    // Just beyond the threshold counts, in both signs, on every analog component
    const double beyond = 2.0 * FMM_INPUT_THRESHOLD;
    TestEqual(TEXT("small forward"), FmmActive(FmmThrust(beyond, 0.0, 0.0)), FMM_CH_THRUST_FORWARD);
    TestEqual(TEXT("small back"), FmmActive(FmmThrust(-beyond, 0.0, 0.0)), FMM_CH_THRUST_BACK);
    TestEqual(TEXT("small right"), FmmActive(FmmThrust(0.0, beyond, 0.0)), FMM_CH_THRUST_RIGHT);
    TestEqual(TEXT("small left"), FmmActive(FmmThrust(0.0, -beyond, 0.0)), FMM_CH_THRUST_LEFT);
    TestEqual(TEXT("small up"), FmmActive(FmmThrust(0.0, 0.0, beyond)), FMM_CH_THRUST_UP);
    TestEqual(TEXT("small down"), FmmActive(FmmThrust(0.0, 0.0, -beyond)), FMM_CH_THRUST_DOWN);
    TestEqual(TEXT("small roll right"), FmmActive(FmmRoll(beyond)), FMM_CH_ROLL_RIGHT);
    TestEqual(TEXT("small roll left"), FmmActive(FmmRoll(-beyond)), FMM_CH_ROLL_LEFT);
    TestEqual(TEXT("small steer right"), FmmActive(FmmSteer(beyond, 0.0)), FMM_CH_STEER_RIGHT);
    TestEqual(TEXT("small steer left"), FmmActive(FmmSteer(-beyond, 0.0)), FMM_CH_STEER_LEFT);
    TestEqual(TEXT("small steer up"), FmmActive(FmmSteer(0.0, beyond)), FMM_CH_STEER_UP);
    TestEqual(TEXT("small steer down"), FmmActive(FmmSteer(0.0, -beyond)), FMM_CH_STEER_DOWN);

    // At or below the threshold (strictly greater is required), in every component at once, sets nothing
    const double quiet[] = { FMM_INPUT_THRESHOLD, -FMM_INPUT_THRESHOLD, 0.5 * FMM_INPUT_THRESHOLD, 1.0e-12 };
    for (const double value : quiet)
    {
        FSOLPilotInput input;
        input.Thrust = FVector3d(value, value, value);
        input.Roll = value;
        input.Steer = FVector2d(value, value);
        TestEqual(FString::Printf(TEXT("all components at %g"), value), FmmActive(input), 0);
    }

    // Combinations set every pressed channel and nothing else
    FSOLPilotInput positive = FmmThrust(1.0, 1.0, 1.0);
    positive.Roll = 1.0;
    positive.Steer = FVector2d(1.0, 1.0);
    TestEqual(TEXT("every positive channel"), FmmActive(positive), FMM_CH_THRUST_FORWARD | FMM_CH_THRUST_RIGHT
        | FMM_CH_THRUST_UP | FMM_CH_ROLL_RIGHT | FMM_CH_STEER_RIGHT | FMM_CH_STEER_UP);
    FSOLPilotInput negative = FmmThrust(-1.0, -1.0, -1.0);
    negative.Roll = -1.0;
    negative.Steer = FVector2d(-1.0, -1.0);
    negative.bStopSpin = true;
    TestEqual(TEXT("every negative channel and stop spin"), FmmActive(negative), FMM_CH_THRUST_BACK
        | FMM_CH_THRUST_LEFT | FMM_CH_THRUST_DOWN | FMM_CH_ROLL_LEFT | FMM_CH_STEER_LEFT | FMM_CH_STEER_DOWN
        | FMM_CH_STOP_SPIN);
    FSOLPilotInput mixed = FmmThrust(1.0, -1.0, 0.0);
    mixed.Roll = 0.3;
    mixed.Steer = FVector2d(0.0, -0.2);
    TestEqual(TEXT("mixed signs"), FmmActive(mixed),
        FMM_CH_THRUST_FORWARD | FMM_CH_THRUST_LEFT | FMM_CH_ROLL_RIGHT | FMM_CH_STEER_DOWN);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathActiveChannelsNonFiniteTest,
    "SOLTest.FlightModeMath.ActiveChannelsNonFinite",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// NaN and infinite components set no channel; a real component or stop spin beside one still sets its own
bool FSOLFlightModeMathActiveChannelsNonFiniteTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    FSOLPilotInput nanEverywhere;
    nanEverywhere.Thrust = FVector3d(FmmNaN(), FmmNaN(), FmmNaN());
    nanEverywhere.Roll = FmmNaN();
    nanEverywhere.Steer = FVector2d(FmmNaN(), FmmNaN());
    TestEqual(TEXT("NaN everywhere"), FmmActive(nanEverywhere), 0);
    TestEqual(TEXT("+inf thrust"), FmmActive(FmmThrust(FmmInf(), 0.0, 0.0)), 0);
    TestEqual(TEXT("-inf thrust"), FmmActive(FmmThrust(0.0, 0.0, -FmmInf())), 0);
    TestEqual(TEXT("-inf roll"), FmmActive(FmmRoll(-FmmInf())), 0);
    TestEqual(TEXT("inf steer"), FmmActive(FmmSteer(FmmInf(), -FmmInf())), 0);

    FSOLPilotInput nanBesideRoll = FmmThrust(FmmNaN(), 0.0, 0.0);
    nanBesideRoll.Roll = 1.0;
    TestEqual(TEXT("NaN thrust beside a real roll"), FmmActive(nanBesideRoll), FMM_CH_ROLL_RIGHT);
    TestEqual(TEXT("NaN X beside a real strafe left"), FmmActive(FmmThrust(FmmNaN(), -1.0, 0.0)), FMM_CH_THRUST_LEFT);
    FSOLPilotInput nanWithStop = nanEverywhere;
    nanWithStop.bStopSpin = true;
    TestEqual(TEXT("stop spin beside NaN"), FmmActive(nanWithStop), FMM_CH_STOP_SPIN);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathManualInputTest, "SOLTest.FlightModeMath.ManualInputDetection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// IsManualInput is exactly "any channel active", over pressed, quiet, noisy and non-finite inputs
bool FSOLFlightModeMathManualInputTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    TestEqual(TEXT("threshold constant"), MANUAL_INPUT_THRESHOLD, FMM_INPUT_THRESHOLD);
    TestFalse(TEXT("nothing pressed"), IsManualInput(FmmNoInput()));
    TestTrue(TEXT("stop spin"), IsManualInput(FmmStopSpin()));

    TArray<FSOLPilotInput> inputs;
    for (const FFmmChannelCase& entry : FmmChannelCases())
    {
        inputs.Add(entry.Input);
    }
    inputs.Add(FmmNoInput());
    inputs.Add(FmmThrust(FMM_INPUT_THRESHOLD, -FMM_INPUT_THRESHOLD, 0.0));
    inputs.Add(FmmSteer(0.5 * FMM_INPUT_THRESHOLD, 1.0e-12));
    inputs.Add(FmmRoll(2.0 * FMM_INPUT_THRESHOLD));
    inputs.Add(FmmThrust(FmmNaN(), FmmInf(), -FmmInf()));
    inputs.Add(FmmBoth(FmmThrust(FmmNaN(), 0.0, 0.0), FmmRoll(1.0)));
    inputs.Add(FmmBoth(FmmSteer(FmmNaN(), FmmNaN()), FmmStopSpin()));
    for (int32 index = 0; index < inputs.Num(); ++index)
    {
        TestEqual(FString::Printf(TEXT("input %d: manual iff a channel is active"), index),
            IsManualInput(inputs[index]), FmmActive(inputs[index]) != 0);
    }
    return true;
}

// ================================================================================================================
// The auto cancel rule and the per-channel latch
// ================================================================================================================

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathCancelOnEachInputTest,
    "SOLTest.FlightModeMath.CancelOnEachManualInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every channel cancels an unlatched Auto to Assisted at once; the same input next frame changes nothing more
bool FSOLFlightModeMathCancelOnEachInputTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    for (const FFmmChannelCase& entry : FmmChannelCases())
    {
        FSOLFlightModeState state = FmmAutoState(FmmNoInput());
        TestTrue(FString::Printf(TEXT("%s cancels"), entry.Name), ApplyPilotInput(state, entry.Input, true));
        TestTrue(FString::Printf(TEXT("%s -> Assisted"), entry.Name), state.Mode == ESOLFlightMode::Assisted);
        TestEqual(FString::Printf(TEXT("%s: latch clear"), entry.Name), FmmLatched(state), 0);
        TestFalse(FString::Printf(TEXT("%s: next frame no change"), entry.Name),
            ApplyPilotInput(state, entry.Input, true));
        TestTrue(FString::Printf(TEXT("%s: stays Assisted"), entry.Name), state.Mode == ESOLFlightMode::Assisted);
    }

    // Auto entered from Manual also cancels to Assisted, not back to Manual
    FSOLFlightModeState fromManual = MakeInitialState(false);
    EngageAuto(fromManual, FmmNoInput());
    ApplyPilotInput(fromManual, FmmThrust(1.0, 0.0, 0.0), true);
    TestTrue(TEXT("from Manual -> Assisted"), fromManual.Mode == ESOLFlightMode::Assisted);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathNoCancelBelowThresholdTest,
    "SOLTest.FlightModeMath.NoCancelBelowThreshold",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Zero input, sub-threshold noise in every component and NaN input keep Auto, frame after frame, latching nothing
bool FSOLFlightModeMathNoCancelBelowThresholdTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    FSOLFlightModeState state = FmmAutoState(FmmNoInput());
    FSOLPilotInput noise;
    noise.Thrust = FVector3d(1.0e-4, -1.0e-4, FMM_INPUT_THRESHOLD);
    noise.Roll = -FMM_INPUT_THRESHOLD;
    noise.Steer = FVector2d(5.0e-4, -5.0e-4);
    for (int32 frame = 0; frame < 10; ++frame)
    {
        TestFalse(TEXT("zero input keeps Auto"), ApplyPilotInput(state, FmmNoInput(), true));
        TestFalse(TEXT("noise keeps Auto"), ApplyPilotInput(state, noise, true));
    }
    TestTrue(TEXT("still Auto"), state.Mode == ESOLFlightMode::Auto);
    TestEqual(TEXT("never latched"), FmmLatched(state), 0);

    // NaN input (a bug upstream) does not cancel either
    FSOLPilotInput nanInput = FmmThrust(FmmNaN(), FmmNaN(), FmmNaN());
    TestFalse(TEXT("NaN keeps Auto"), ApplyPilotInput(state, nanInput, true));
    TestTrue(TEXT("Auto after NaN"), state.Mode == ESOLFlightMode::Auto);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathNoCancelOutsideAutoTest,
    "SOLTest.FlightModeMath.NoCancelOutsideAuto",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// In Manual and Assisted, manual input is just flying: the mode never changes and no channel is ever latched
bool FSOLFlightModeMathNoCancelOutsideAutoTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    const FSOLPilotInput inputs[] = { FmmThrust(1.0, 1.0, 1.0), FmmRoll(1.0), FmmSteer(1.0, 1.0), FmmStopSpin(),
        FmmNoInput() };
    const bool controls[] = { true, false };
    for (const bool bControl : controls)
    {
        FSOLFlightModeState manual = MakeInitialState(false);
        FSOLFlightModeState assisted = MakeInitialState(true);
        for (const FSOLPilotInput& input : inputs)
        {
            TestFalse(TEXT("Manual: no change reported"), ApplyPilotInput(manual, input, bControl));
            TestFalse(TEXT("Assisted: no change reported"), ApplyPilotInput(assisted, input, bControl));
        }
        TestTrue(TEXT("Manual kept"), manual.Mode == ESOLFlightMode::Manual);
        TestEqual(TEXT("Manual never latched"), FmmLatched(manual), 0);
        TestTrue(TEXT("Assisted kept"), assisted.Mode == ESOLFlightMode::Assisted);
        TestEqual(TEXT("Assisted never latched"), FmmLatched(assisted), 0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathNoCancelWithoutControlTest,
    "SOLTest.FlightModeMath.NoCancelWithoutControl",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// During G-LOC input never cancels nor latches; once control returns the same (unlatched) input cancels
bool FSOLFlightModeMathNoCancelWithoutControlTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    FSOLFlightModeState state = FmmAutoState(FmmNoInput());
    for (int32 frame = 0; frame < 5; ++frame)
    {
        TestFalse(TEXT("G-LOC thrust ignored"), ApplyPilotInput(state, FmmThrust(1.0, 0.0, 0.0), false));
        TestFalse(TEXT("G-LOC steer ignored"), ApplyPilotInput(state, FmmSteer(1.0, 1.0), false));
        TestFalse(TEXT("G-LOC stop spin ignored"), ApplyPilotInput(state, FmmStopSpin(), false));
    }
    TestTrue(TEXT("still Auto during G-LOC"), state.Mode == ESOLFlightMode::Auto);
    TestEqual(TEXT("G-LOC input does not latch"), FmmLatched(state), 0);

    TestTrue(TEXT("control back: thrust cancels"), ApplyPilotInput(state, FmmThrust(1.0, 0.0, 0.0), true));
    TestTrue(TEXT("-> Assisted"), state.Mode == ESOLFlightMode::Assisted);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathLatchedInputTest, "SOLTest.FlightModeMath.LatchedInputDoesNotCancel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A channel held at engage is ignored while held (any deflection of the same sign); releasing it clears its latch
// without cancelling, and pressing it again cancels
bool FSOLFlightModeMathLatchedInputTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    FSOLFlightModeState state = FmmAutoState(FmmThrust(1.0, 0.0, 0.0));
    TestEqual(TEXT("forward latched at engage"), FmmLatched(state), FMM_CH_THRUST_FORWARD);

    for (int32 frame = 0; frame < 3; ++frame)
    {
        TestFalse(TEXT("held W ignored"), ApplyPilotInput(state, FmmThrust(1.0, 0.0, 0.0), true));
    }
    TestFalse(TEXT("half forward is the same channel"), ApplyPilotInput(state, FmmThrust(0.5, 0.0, 0.0), true));
    TestTrue(TEXT("still Auto"), state.Mode == ESOLFlightMode::Auto);
    TestEqual(TEXT("still latched"), FmmLatched(state), FMM_CH_THRUST_FORWARD);

    // Released: the latch clears and this frame does not cancel
    TestFalse(TEXT("release frame does not cancel"), ApplyPilotInput(state, FmmNoInput(), true));
    TestEqual(TEXT("latch cleared"), FmmLatched(state), 0);
    TestTrue(TEXT("Auto after release"), state.Mode == ESOLFlightMode::Auto);
    TestFalse(TEXT("quiet frame keeps Auto"), ApplyPilotInput(state, FmmNoInput(), true));

    // Pressing the released channel again cancels
    TestTrue(TEXT("W again cancels"), ApplyPilotInput(state, FmmThrust(1.0, 0.0, 0.0), true));
    TestTrue(TEXT("-> Assisted"), state.Mode == ESOLFlightMode::Assisted);
    TestEqual(TEXT("cancel clears the latch"), FmmLatched(state), 0);

    // Dropping to noise level counts as released
    FSOLFlightModeState noisy = FmmAutoState(FmmSteer(0.0, 0.5));
    TestEqual(TEXT("steer up latched"), FmmLatched(noisy), FMM_CH_STEER_UP);
    TestFalse(TEXT("noise frame does not cancel"), ApplyPilotInput(noisy, FmmSteer(0.0, 1.0e-4), true));
    TestEqual(TEXT("noise releases the latch"), FmmLatched(noisy), 0);
    TestTrue(TEXT("noise frame keeps Auto"), IsAuto(noisy));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathOtherChannelCancelsTest,
    "SOLTest.FlightModeMath.OtherChannelCancelsWhileLatched",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Any channel that is not latched cancels Auto at once, even while the latched channels are still held
bool FSOLFlightModeMathOtherChannelCancelsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    const FSOLPilotInput forward = FmmThrust(1.0, 0.0, 0.0);
    for (const FFmmChannelCase& entry : FmmChannelCases())
    {
        // Thrust back shares W's axis (summing would release W); the sign flip has its own test
        if (entry.Channel == FMM_CH_THRUST_FORWARD || entry.Channel == FMM_CH_THRUST_BACK)
        {
            continue;
        }
        FSOLFlightModeState state = FmmAutoState(forward);
        TestFalse(FString::Printf(TEXT("%s: W alone held"), entry.Name), ApplyPilotInput(state, forward, true));
        TestTrue(FString::Printf(TEXT("W held, %s cancels at once"), entry.Name),
            ApplyPilotInput(state, FmmBoth(forward, entry.Input), true));
        TestTrue(FString::Printf(TEXT("%s -> Assisted"), entry.Name), state.Mode == ESOLFlightMode::Assisted);
        TestEqual(FString::Printf(TEXT("%s: latch cleared"), entry.Name), FmmLatched(state), 0);
    }

    // Two channels latched, a third pressed while both are held: cancels
    const FSOLPilotInput forwardRight = FmmThrust(1.0, 1.0, 0.0);
    FSOLFlightModeState two = FmmAutoState(forwardRight);
    TestTrue(TEXT("W + D held, roll cancels"), ApplyPilotInput(two, FmmBoth(forwardRight, FmmRoll(-1.0)), true));
    TestTrue(TEXT("W + D + roll -> Assisted"), two.Mode == ESOLFlightMode::Assisted);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathSignFlipCancelsTest, "SOLTest.FlightModeMath.SignFlipCancels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Channels are signed: reversing a latched axis is a different, unlatched channel and cancels in that frame
bool FSOLFlightModeMathSignFlipCancelsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    struct FFlip
    {
        const TCHAR* Name;
        FSOLPilotInput Held;
        FSOLPilotInput Flipped;
    };
    const FFlip flips[] =
    {
        { TEXT("forward -> back"), FmmThrust(1.0, 0.0, 0.0), FmmThrust(-1.0, 0.0, 0.0) },
        { TEXT("left -> right"), FmmThrust(0.0, -1.0, 0.0), FmmThrust(0.0, 1.0, 0.0) },
        { TEXT("up -> down"), FmmThrust(0.0, 0.0, 1.0), FmmThrust(0.0, 0.0, -1.0) },
        { TEXT("roll right -> left"), FmmRoll(1.0), FmmRoll(-1.0) },
        { TEXT("steer right -> left"), FmmSteer(0.4, 0.0), FmmSteer(-0.4, 0.0) },
        { TEXT("steer down -> up"), FmmSteer(0.0, -0.4), FmmSteer(0.0, 0.4) },
    };
    for (const FFlip& flip : flips)
    {
        FSOLFlightModeState state = FmmAutoState(flip.Held);
        TestFalse(FString::Printf(TEXT("%s: held does not cancel"), flip.Name),
            ApplyPilotInput(state, flip.Held, true));
        TestTrue(FString::Printf(TEXT("%s: flip cancels"), flip.Name), ApplyPilotInput(state, flip.Flipped, true));
        TestTrue(FString::Printf(TEXT("%s -> Assisted"), flip.Name), state.Mode == ESOLFlightMode::Assisted);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathPartialReleaseTest, "SOLTest.FlightModeMath.PartialRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Latched channels release one by one: a released one is cleared (and cancels if pressed again) while the others stay
// latched; releasing everything clears the whole mask without cancelling
bool FSOLFlightModeMathPartialReleaseTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    const FSOLPilotInput forward = FmmThrust(1.0, 0.0, 0.0);
    const FSOLPilotInput forwardRight = FmmThrust(1.0, 1.0, 0.0);
    FSOLFlightModeState state = FmmAutoState(forwardRight);
    TestEqual(TEXT("W + D latched"), FmmLatched(state), FMM_CH_THRUST_FORWARD | FMM_CH_THRUST_RIGHT);
    TestFalse(TEXT("both held"), ApplyPilotInput(state, forwardRight, true));

    TestFalse(TEXT("D released: no cancel"), ApplyPilotInput(state, forward, true));
    TestEqual(TEXT("D cleared, W still latched"), FmmLatched(state), FMM_CH_THRUST_FORWARD);
    TestFalse(TEXT("W still held: no cancel"), ApplyPilotInput(state, forward, true));
    TestTrue(TEXT("still Auto"), IsAuto(state));

    TestTrue(TEXT("D pressed again cancels"), ApplyPilotInput(state, forwardRight, true));
    TestTrue(TEXT("-> Assisted"), state.Mode == ESOLFlightMode::Assisted);
    TestEqual(TEXT("cancel clears the latch"), FmmLatched(state), 0);

    // Releasing everything at once clears every latch and keeps Auto
    const FSOLPilotInput many = FmmBoth(FmmBoth(forwardRight, FmmRoll(-1.0)), FmmStopSpin());
    FSOLFlightModeState all = FmmAutoState(many);
    TestEqual(TEXT("four latched"), FmmLatched(all),
        FMM_CH_THRUST_FORWARD | FMM_CH_THRUST_RIGHT | FMM_CH_ROLL_LEFT | FMM_CH_STOP_SPIN);
    TestFalse(TEXT("full release: no cancel"), ApplyPilotInput(all, FmmNoInput(), true));
    TestEqual(TEXT("full release clears all"), FmmLatched(all), 0);
    TestTrue(TEXT("Auto after full release"), IsAuto(all));
    TestTrue(TEXT("a formerly latched channel now cancels"), ApplyPilotInput(all, FmmStopSpin(), true));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathLatchNoControlTest,
    "SOLTest.FlightModeMath.LatchClearsWithoutControl",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// During G-LOC a released channel's latch still clears, but held input neither cancels nor adds latches; once control
// returns an unlatched held channel cancels and a latched one held throughout does not
bool FSOLFlightModeMathLatchNoControlTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    const FSOLPilotInput down = FmmThrust(0.0, 0.0, -1.0);
    const FSOLPilotInput downForward = FmmThrust(1.0, 0.0, -1.0);
    FSOLFlightModeState state = FmmAutoState(down);
    TestEqual(TEXT("down latched"), FmmLatched(state), FMM_CH_THRUST_DOWN);
    TestFalse(TEXT("held during G-LOC: no cancel"), ApplyPilotInput(state, down, false));
    TestEqual(TEXT("held during G-LOC: still latched"), FmmLatched(state), FMM_CH_THRUST_DOWN);
    TestFalse(TEXT("forward added during G-LOC: no cancel"), ApplyPilotInput(state, downForward, false));
    TestEqual(TEXT("G-LOC input adds no latch"), FmmLatched(state), FMM_CH_THRUST_DOWN);
    TestTrue(TEXT("Auto during G-LOC"), IsAuto(state));
    TestTrue(TEXT("control back, forward unlatched: cancels"), ApplyPilotInput(state, downForward, true));
    TestTrue(TEXT("-> Assisted"), state.Mode == ESOLFlightMode::Assisted);

    // Released during G-LOC: the latch clears, and the quiet return of control keeps Auto
    FSOLFlightModeState released = FmmAutoState(down);
    TestFalse(TEXT("released during G-LOC"), ApplyPilotInput(released, FmmNoInput(), false));
    TestEqual(TEXT("latch cleared during G-LOC"), FmmLatched(released), 0);
    TestFalse(TEXT("control back, nothing held"), ApplyPilotInput(released, FmmNoInput(), true));
    TestTrue(TEXT("still Auto"), IsAuto(released));

    // A latched channel held all through G-LOC still does not cancel once control returns
    FSOLFlightModeState heldThrough = FmmAutoState(FmmRoll(1.0));
    ApplyPilotInput(heldThrough, FmmRoll(1.0), false);
    TestFalse(TEXT("control back, roll still held"), ApplyPilotInput(heldThrough, FmmRoll(1.0), true));
    TestEqual(TEXT("roll still latched"), FmmLatched(heldThrough), FMM_CH_ROLL_RIGHT);
    TestTrue(TEXT("Auto kept"), IsAuto(heldThrough));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathRelatchInputTest, "SOLTest.FlightModeMath.RelatchInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// In Auto, input held when a map / menu / panel closes is added to the latch (existing latches kept) and ignored until
// released, like at engage
bool FSOLFlightModeMathRelatchInputTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    const FSOLPilotInput forward = FmmThrust(1.0, 0.0, 0.0);
    const FSOLPilotInput forwardRollLeft = FmmBoth(forward, FmmRoll(-1.0));
    FSOLFlightModeState state = FmmAutoState(forward);
    RelatchInput(state, FmmRoll(-1.0));
    TestTrue(TEXT("relatch keeps Auto"), IsAuto(state));
    TestEqual(TEXT("roll left added, W kept"), FmmLatched(state), FMM_CH_THRUST_FORWARD | FMM_CH_ROLL_LEFT);

    // Nothing, noise or NaN held adds nothing and removes nothing
    RelatchInput(state, FmmNoInput());
    RelatchInput(state, FmmSteer(0.5 * FMM_INPUT_THRESHOLD, 0.0));
    RelatchInput(state, FmmThrust(FmmNaN(), FmmInf(), 0.0));
    TestEqual(TEXT("quiet relatch changes nothing"), FmmLatched(state), FMM_CH_THRUST_FORWARD | FMM_CH_ROLL_LEFT);

    TestFalse(TEXT("both held: no cancel"), ApplyPilotInput(state, forwardRollLeft, true));
    TestFalse(TEXT("roll released: no cancel"), ApplyPilotInput(state, forward, true));
    TestEqual(TEXT("roll cleared, W kept"), FmmLatched(state), FMM_CH_THRUST_FORWARD);
    TestTrue(TEXT("roll again cancels"), ApplyPilotInput(state, forwardRollLeft, true));
    TestTrue(TEXT("-> Assisted"), state.Mode == ESOLFlightMode::Assisted);

    // From an unlatched Auto: the map closes with the stick deflected
    FSOLFlightModeState stick = FmmAutoState(FmmNoInput());
    RelatchInput(stick, FmmSteer(0.4, 0.0));
    TestEqual(TEXT("steer right latched"), FmmLatched(stick), FMM_CH_STEER_RIGHT);
    TestFalse(TEXT("deflected stick: no cancel"), ApplyPilotInput(stick, FmmSteer(0.4, 0.0), true));
    TestFalse(TEXT("stick released: no cancel"), ApplyPilotInput(stick, FmmNoInput(), true));
    TestEqual(TEXT("stick latch cleared"), FmmLatched(stick), 0);
    TestTrue(TEXT("stick again cancels"), ApplyPilotInput(stick, FmmSteer(0.4, 0.0), true));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathRelatchOutsideAutoTest,
    "SOLTest.FlightModeMath.RelatchOutsideAuto",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Outside Auto RelatchInput has no effect at all: the mode is unchanged and nothing is latched
bool FSOLFlightModeMathRelatchOutsideAutoTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    FSOLPilotInput everything = FmmThrust(1.0, 1.0, 1.0);
    everything.Roll = 1.0;
    everything.Steer = FVector2d(1.0, 1.0);
    everything.bStopSpin = true;

    FSOLFlightModeState manual = MakeInitialState(false);
    RelatchInput(manual, everything);
    TestTrue(TEXT("Manual kept"), manual.Mode == ESOLFlightMode::Manual);
    TestEqual(TEXT("Manual: nothing latched"), FmmLatched(manual), 0);

    FSOLFlightModeState assisted = MakeInitialState(true);
    RelatchInput(assisted, everything);
    TestTrue(TEXT("Assisted kept"), assisted.Mode == ESOLFlightMode::Assisted);
    TestEqual(TEXT("Assisted: nothing latched"), FmmLatched(assisted), 0);

    // After a disengage the state is Assisted again, so a relatch does nothing there either
    FSOLFlightModeState disengaged = FmmAutoState(FmmThrust(1.0, 0.0, 0.0));
    DisengageAuto(disengaged);
    RelatchInput(disengaged, everything);
    TestTrue(TEXT("disengaged stays Assisted"), disengaged.Mode == ESOLFlightMode::Assisted);
    TestEqual(TEXT("disengaged: nothing latched"), FmmLatched(disengaged), 0);
    return true;
}

// ================================================================================================================
// Steering gate
// ================================================================================================================

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathSteerGateHoldTest, "SOLTest.FlightModeMath.SteerGateHold",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Holding Space steers until it is released; the stick recenters on each press edge only
bool FSOLFlightModeMathSteerGateHoldTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    FSOLSteerGateState gate = MakeSteerGate(false);
    FSOLSteerGateUpdate update = UpdateSteerGate(gate, false);
    TestFalse(TEXT("idle: inactive"), update.bActive);
    TestFalse(TEXT("idle: no recenter"), update.bRecenter);

    SetSteerHold(gate, true);
    TestTrue(TEXT("hold recorded"), gate.bHoldDown);
    update = UpdateSteerGate(gate, false);
    TestTrue(TEXT("press: active"), update.bActive);
    TestTrue(TEXT("press: recenter"), update.bRecenter);
    update = UpdateSteerGate(gate, false);
    TestTrue(TEXT("held: active"), update.bActive);
    TestFalse(TEXT("held: no recenter"), update.bRecenter);

    SetSteerHold(gate, false);
    update = UpdateSteerGate(gate, false);
    TestFalse(TEXT("release: inactive"), update.bActive);
    TestFalse(TEXT("release: no recenter"), update.bRecenter);
    TestFalse(TEXT("toggle untouched"), gate.bToggleOn);

    SetSteerHold(gate, true);
    update = UpdateSteerGate(gate, false);
    TestTrue(TEXT("second press: active"), update.bActive);
    TestTrue(TEXT("second press: recenter again"), update.bRecenter);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathSteerGateToggleTest, "SOLTest.FlightModeMath.SteerGateToggle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A wheel click latches steering on until the next click; recenter on each turn-on
bool FSOLFlightModeMathSteerGateToggleTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    FSOLSteerGateState gate = MakeSteerGate(false);
    ToggleSteer(gate);
    TestTrue(TEXT("toggle on"), gate.bToggleOn);
    FSOLSteerGateUpdate update = UpdateSteerGate(gate, false);
    TestTrue(TEXT("on: active"), update.bActive);
    TestTrue(TEXT("on: recenter"), update.bRecenter);
    for (int32 frame = 0; frame < 3; ++frame)
    {
        update = UpdateSteerGate(gate, false);
        TestTrue(TEXT("persists: active"), update.bActive);
        TestFalse(TEXT("persists: no recenter"), update.bRecenter);
    }

    ToggleSteer(gate);
    TestFalse(TEXT("toggle off"), gate.bToggleOn);
    update = UpdateSteerGate(gate, false);
    TestFalse(TEXT("off: inactive"), update.bActive);
    TestFalse(TEXT("off: no recenter"), update.bRecenter);

    ToggleSteer(gate);
    update = UpdateSteerGate(gate, false);
    TestTrue(TEXT("on again: recenter"), update.bRecenter);

    // Two clicks between updates leave it as it was: no edge
    ToggleSteer(gate);
    ToggleSteer(gate);
    update = UpdateSteerGate(gate, false);
    TestTrue(TEXT("double click: still active"), update.bActive);
    TestFalse(TEXT("double click: no recenter"), update.bRecenter);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathSteerGateOrTest, "SOLTest.FlightModeMath.SteerGateHoldAndToggleOr",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Hold OR toggle: a second source joining or leaving does not recenter or stop steering
bool FSOLFlightModeMathSteerGateOrTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    FSOLSteerGateState gate = MakeSteerGate(false);
    ToggleSteer(gate);
    TestTrue(TEXT("toggle on: recenter"), UpdateSteerGate(gate, false).bRecenter);

    SetSteerHold(gate, true);
    FSOLSteerGateUpdate update = UpdateSteerGate(gate, false);
    TestTrue(TEXT("hold joins: active"), update.bActive);
    TestFalse(TEXT("hold joins: no recenter"), update.bRecenter);

    SetSteerHold(gate, false);
    update = UpdateSteerGate(gate, false);
    TestTrue(TEXT("hold leaves, toggle on: active"), update.bActive);
    TestFalse(TEXT("hold leaves: no recenter"), update.bRecenter);

    // Toggle off while Space is held: still steering until Space is released
    SetSteerHold(gate, true);
    UpdateSteerGate(gate, false);
    ToggleSteer(gate);
    update = UpdateSteerGate(gate, false);
    TestTrue(TEXT("toggle off while held: active"), update.bActive);
    TestFalse(TEXT("toggle off while held: no recenter"), update.bRecenter);
    SetSteerHold(gate, false);
    update = UpdateSteerGate(gate, false);
    TestFalse(TEXT("both off: inactive"), update.bActive);

    // Hold first, then the toggle turns on: no extra recenter
    FSOLSteerGateState holdFirst = MakeSteerGate(false);
    SetSteerHold(holdFirst, true);
    TestTrue(TEXT("hold first: recenter"), UpdateSteerGate(holdFirst, false).bRecenter);
    ToggleSteer(holdFirst);
    update = UpdateSteerGate(holdFirst, false);
    TestTrue(TEXT("toggle joins: active"), update.bActive);
    TestFalse(TEXT("toggle joins: no recenter"), update.bRecenter);
    SetSteerHold(holdFirst, false);
    TestTrue(TEXT("release with toggle on: active"), UpdateSteerGate(holdFirst, false).bActive);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathSteerGateSuspensionTest,
    "SOLTest.FlightModeMath.SteerGateSuspension",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Menus / map / panel / focus loss suspend steering without losing the toggle; resuming recenters
bool FSOLFlightModeMathSteerGateSuspensionTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    FSOLSteerGateState gate = MakeSteerGate(true);
    TestTrue(TEXT("first update recenters"), UpdateSteerGate(gate, false).bRecenter);

    FSOLSteerGateUpdate update = UpdateSteerGate(gate, true);
    TestFalse(TEXT("suspended: inactive"), update.bActive);
    TestFalse(TEXT("suspended: no recenter"), update.bRecenter);
    TestTrue(TEXT("suspension keeps the toggle"), gate.bToggleOn);
    update = UpdateSteerGate(gate, true);
    TestFalse(TEXT("still suspended: inactive"), update.bActive);

    update = UpdateSteerGate(gate, false);
    TestTrue(TEXT("resume: active"), update.bActive);
    TestTrue(TEXT("resume: recenter"), update.bRecenter);
    TestFalse(TEXT("resume steady: no recenter"), UpdateSteerGate(gate, false).bRecenter);

    // Suspension never edits the inputs itself
    FSOLSteerGateState held = MakeSteerGate(false);
    SetSteerHold(held, true);
    UpdateSteerGate(held, true);
    TestTrue(TEXT("update leaves the hold alone"), held.bHoldDown);
    TestFalse(TEXT("update leaves the toggle alone"), held.bToggleOn);

    // The pawn releases the hold on suspension (flushed keys); after resume nothing steers
    FSOLSteerGateState flushed = MakeSteerGate(false);
    SetSteerHold(flushed, true);
    TestTrue(TEXT("hold active"), UpdateSteerGate(flushed, false).bActive);
    SetSteerHold(flushed, false);
    TestFalse(TEXT("map opens: inactive"), UpdateSteerGate(flushed, true).bActive);
    update = UpdateSteerGate(flushed, false);
    TestFalse(TEXT("map closes, hold gone: inactive"), update.bActive);
    TestFalse(TEXT("map closes, hold gone: no recenter"), update.bRecenter);

    // Toggle clicked while suspended takes effect (with a recenter) on resume
    FSOLSteerGateState clicked = MakeSteerGate(false);
    UpdateSteerGate(clicked, true);
    ToggleSteer(clicked);
    TestFalse(TEXT("clicked while suspended: inactive"), UpdateSteerGate(clicked, true).bActive);
    update = UpdateSteerGate(clicked, false);
    TestTrue(TEXT("resume after click: active"), update.bActive);
    TestTrue(TEXT("resume after click: recenter"), update.bRecenter);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathSteerGateInitialTest,
    "SOLTest.FlightModeMath.SteerGateInitialToggle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A gate made with the toggle on recenters on its first update; one made off stays inactive
bool FSOLFlightModeMathSteerGateInitialTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    FSOLSteerGateState on = MakeSteerGate(true);
    TestTrue(TEXT("on: toggle"), on.bToggleOn);
    TestFalse(TEXT("on: no hold"), on.bHoldDown);
    TestFalse(TEXT("on: was not active"), on.bWasActive);
    FSOLSteerGateUpdate update = UpdateSteerGate(on, false);
    TestTrue(TEXT("on: active"), update.bActive);
    TestTrue(TEXT("on: recenter at start"), update.bRecenter);
    TestTrue(TEXT("on: remembers activity"), on.bWasActive);

    // Starting suspended (e.g. the spawn happens under a menu) delays the recenter to the resume
    FSOLSteerGateState underMenu = MakeSteerGate(true);
    TestFalse(TEXT("under menu: inactive"), UpdateSteerGate(underMenu, true).bActive);
    TestTrue(TEXT("menu closes: recenter"), UpdateSteerGate(underMenu, false).bRecenter);

    FSOLSteerGateState off = MakeSteerGate(false);
    TestFalse(TEXT("off: toggle"), off.bToggleOn);
    update = UpdateSteerGate(off, false);
    TestFalse(TEXT("off: inactive"), update.bActive);
    TestFalse(TEXT("off: no recenter"), update.bRecenter);

    const FSOLSteerGateState defaulted;
    TestFalse(TEXT("default: toggle off"), defaulted.bToggleOn);
    TestFalse(TEXT("default: no hold"), defaulted.bHoldDown);
    TestFalse(TEXT("default: not active"), defaulted.bWasActive);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathSteerGateStickTest, "SOLTest.FlightModeMath.SteerGateGatesStick",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The stick command passes only while steering is active, so mouse motion with steering off never cancels Auto
bool FSOLFlightModeMathSteerGateStickTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    const FVector2d stick(0.3, -0.4);
    TestEqual(TEXT("active passes yaw"), GateSteer(stick, true).X, 0.3);
    TestEqual(TEXT("active passes pitch"), GateSteer(stick, true).Y, -0.4);
    TestEqual(TEXT("inactive zero yaw"), GateSteer(stick, false).X, 0.0);
    TestEqual(TEXT("inactive zero pitch"), GateSteer(stick, false).Y, 0.0);
    TestTrue(TEXT("inactive zero vector"), GateSteer(FVector2d(1.0, 1.0), false).IsZero());

    // Steering off: a full-deflection stick is gated to zero, so Auto stays engaged
    FSOLSteerGateState gate = MakeSteerGate(false);
    const FSOLSteerGateUpdate off = UpdateSteerGate(gate, false);
    FSOLFlightModeState state = FmmAutoState(FmmNoInput());
    FSOLPilotInput input;
    input.Steer = GateSteer(FVector2d(1.0, 1.0), off.bActive);
    TestFalse(TEXT("gated stick is not manual input"), IsManualInput(input));
    TestFalse(TEXT("gated stick keeps Auto"), ApplyPilotInput(state, input, true));
    TestTrue(TEXT("Auto kept"), IsAuto(state));

    // Steering on: the same stick cancels
    SetSteerHold(gate, true);
    const FSOLSteerGateUpdate on = UpdateSteerGate(gate, false);
    input.Steer = GateSteer(FVector2d(1.0, 1.0), on.bActive);
    TestTrue(TEXT("live stick cancels"), ApplyPilotInput(state, input, true));
    TestTrue(TEXT("-> Assisted"), state.Mode == ESOLFlightMode::Assisted);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathSteerIconTest, "SOLTest.FlightModeMath.SteerIcon",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// HUD icon: Locked (toggle) beats Held (Space) beats Free; suspension does not change it
bool FSOLFlightModeMathSteerIconTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    FSOLSteerGateState gate = MakeSteerGate(false);
    TestTrue(TEXT("idle: Free"), SteerIconFor(gate) == ESOLSteerIcon::Free);
    SetSteerHold(gate, true);
    TestTrue(TEXT("hold: Held"), SteerIconFor(gate) == ESOLSteerIcon::Held);
    ToggleSteer(gate);
    TestTrue(TEXT("hold and toggle: Locked"), SteerIconFor(gate) == ESOLSteerIcon::Locked);
    SetSteerHold(gate, false);
    TestTrue(TEXT("toggle only: Locked"), SteerIconFor(gate) == ESOLSteerIcon::Locked);
    UpdateSteerGate(gate, true);
    TestTrue(TEXT("suspended toggle: Locked"), SteerIconFor(gate) == ESOLSteerIcon::Locked);
    ToggleSteer(gate);
    TestTrue(TEXT("all off: Free"), SteerIconFor(gate) == ESOLSteerIcon::Free);
    return true;
}

// ================================================================================================================
// Boost toggle
// ================================================================================================================

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathBoostToggleTest, "SOLTest.FlightModeMath.BoostToggle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Tab flips boost and returns the new state; it stays on across updates while the pilot has control
bool FSOLFlightModeMathBoostToggleTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    FSOLBoostToggle boost;
    TestFalse(TEXT("starts off"), boost.bOn);
    TestTrue(TEXT("toggle returns on"), ToggleBoost(boost, true));
    TestTrue(TEXT("on"), boost.bOn);
    for (int32 frame = 0; frame < 5; ++frame)
    {
        UpdateBoost(boost, true);
    }
    TestTrue(TEXT("stays on with control"), boost.bOn);
    TestFalse(TEXT("toggle returns off"), ToggleBoost(boost, true));
    TestFalse(TEXT("off"), boost.bOn);
    UpdateBoost(boost, true);
    TestFalse(TEXT("update never turns it on"), boost.bOn);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathBoostNoControlTest, "SOLTest.FlightModeMath.BoostOffWithoutControl",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// G-LOC turns boost off, refuses the toggle, and the pilot wakes up without boost
bool FSOLFlightModeMathBoostNoControlTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    FSOLBoostToggle boost;
    ToggleBoost(boost, true);
    TestTrue(TEXT("refused toggle returns the unchanged state"), ToggleBoost(boost, false));
    TestTrue(TEXT("still on before the update"), boost.bOn);

    UpdateBoost(boost, false);
    TestFalse(TEXT("G-LOC turns it off"), boost.bOn);
    TestFalse(TEXT("toggle during G-LOC refused"), ToggleBoost(boost, false));
    TestFalse(TEXT("still off during G-LOC"), boost.bOn);
    UpdateBoost(boost, true);
    TestFalse(TEXT("wakes up off"), boost.bOn);
    TestTrue(TEXT("toggle works again"), ToggleBoost(boost, true));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathBoostResetTest, "SOLTest.FlightModeMath.BoostReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A jump arrival or respawn clears boost; resetting an off boost keeps it off
bool FSOLFlightModeMathBoostResetTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLFlightMode;
    FSOLBoostToggle boost;
    ToggleBoost(boost, true);
    ResetBoost(boost);
    TestFalse(TEXT("reset turns it off"), boost.bOn);
    ResetBoost(boost);
    TestFalse(TEXT("reset off stays off"), boost.bOn);
    return true;
}

// ================================================================================================================
// Key layout and settings migration
// ================================================================================================================

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathKeyDefaultsTest, "SOLTest.FlightModeMath.KeyDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The issue #19 layout: two new bindings after StopSpin and the moved defaults
bool FSOLFlightModeMathKeyDefaultsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    TestEqual(TEXT("binding count"), BINDING_COUNT, FMM_BINDING_COUNT);

    const int32 stopSpin = FmmBinding(TEXT("StopSpin"));
    const int32 steerHold = FmmBinding(TEXT("SteerHold"));
    const int32 steerToggle = FmmBinding(TEXT("SteerToggle"));
    TestNotEqual(TEXT("SteerHold exists"), steerHold, static_cast<int32>(INDEX_NONE));
    TestNotEqual(TEXT("SteerToggle exists"), steerToggle, static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("SteerHold right after StopSpin"), steerHold, stopSpin + 1);
    TestEqual(TEXT("SteerToggle right after SteerHold"), steerToggle, stopSpin + 2);
    TestEqual(TEXT("enum SteerHold"), static_cast<int32>(EBinding::SteerHold), steerHold);
    TestEqual(TEXT("enum SteerToggle"), static_cast<int32>(EBinding::SteerToggle), steerToggle);

    // The eight bindings issue #19 sets (changed, new or explicitly kept)
    struct FExpected
    {
        const TCHAR* Id;
        const TCHAR* Key;
    };
    const FExpected expected[] =
    {
        { TEXT("ThrustUp"), TEXT("LeftShift") },
        { TEXT("ThrustDown"), TEXT("LeftControl") },
        { TEXT("Boost"), TEXT("Tab") },
        { TEXT("ToggleAssist"), TEXT("C") },
        { TEXT("StopSpin"), TEXT("Z") },
        { TEXT("SteerHold"), TEXT("SpaceBar") },
        { TEXT("SteerToggle"), TEXT("MiddleMouseButton") },
        { TEXT("RecenterStick"), TEXT("B") },
    };
    const TArray<FString> defaults = DefaultKeys();
    TestEqual(TEXT("one default per binding"), defaults.Num(), FMM_BINDING_COUNT);
    for (const FExpected& entry : expected)
    {
        TestEqual(FString::Printf(TEXT("%s default"), entry.Id), FmmKeyOf(defaults, entry.Id), FString(entry.Key));
        const int32 binding = FmmBinding(entry.Id);
        if (binding != INDEX_NONE)
        {
            TestEqual(FString::Printf(TEXT("%s def default"), entry.Id), FString(GetDef(binding).DefaultKey),
                FString(entry.Key));
        }
    }

    // A few untouched defaults stay where they were
    TestEqual(TEXT("forward still W"), FmmKeyOf(defaults, TEXT("ThrustForward")), FString(TEXT("W")));
    TestEqual(TEXT("fire still left mouse"), FmmKeyOf(defaults, TEXT("Fire")), FString(TEXT("LeftMouseButton")));
    TestEqual(TEXT("free look still Alt"), FmmKeyOf(defaults, TEXT("FreeLook")), FString(TEXT("LeftAlt")));
    TestEqual(TEXT("jump map on J (issue #20)"), FmmKeyOf(defaults, TEXT("OpenMapJump")), FString(TEXT("J")));
    TestEqual(TEXT("map on M (issue #20)"), FmmKeyOf(defaults, TEXT("OpenMap")), FString(TEXT("M")));
    TestEqual(TEXT("ToggleMap removed (issue #20)"), FmmBinding(TEXT("ToggleMap")), static_cast<int32>(INDEX_NONE));

    // The new rows are labelled for Settings and Controls
    if (steerHold != INDEX_NONE && steerToggle != INDEX_NONE)
    {
        TestFalse(TEXT("SteerHold label"), FString(GetDef(steerHold).Label).IsEmpty());
        TestFalse(TEXT("SteerHold area"), FString(GetDef(steerHold).Area).IsEmpty());
        TestFalse(TEXT("SteerToggle label"), FString(GetDef(steerToggle).Label).IsEmpty());
        TestFalse(TEXT("SteerToggle area"), FString(GetDef(steerToggle).Area).IsEmpty());
        TestNotEqual(TEXT("distinct labels"), FString(GetDef(steerHold).Label), FString(GetDef(steerToggle).Label));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathKeyDefaultsBindableTest,
    "SOLTest.FlightModeMath.KeyDefaultsBindable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The new defaults are bindable, unreserved and unshared; the reserved and map-fixed rules still hold
bool FSOLFlightModeMathKeyDefaultsBindableTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    const TArray<FString> defaults = DefaultKeys();
    const TCHAR* ids[] = { TEXT("ThrustUp"), TEXT("Boost"), TEXT("ToggleAssist"), TEXT("SteerHold"),
        TEXT("SteerToggle"), TEXT("RecenterStick") };
    for (const TCHAR* id : ids)
    {
        const int32 binding = FmmBinding(id);
        if (!TestNotEqual(FString::Printf(TEXT("%s exists"), id), binding, static_cast<int32>(INDEX_NONE)))
        {
            continue;
        }
        const FString& key = defaults[binding];
        TestTrue(FString::Printf(TEXT("%s default bindable to it"), id), CanBindKeyTo(binding, key));
        TestFalse(FString::Printf(TEXT("%s default not reserved"), id), IsReservedKey(key));
        TestFalse(FString::Printf(TEXT("%s default not shared"), id), HasConflict(defaults, binding));
        TestFalse(FString::Printf(TEXT("%s refuses F1"), id), CanBindKeyTo(binding, TEXT("F1")));
        TestFalse(FString::Printf(TEXT("%s refuses F2"), id), CanBindKeyTo(binding, TEXT("F2")));
        TestFalse(FString::Printf(TEXT("%s refuses Escape"), id), CanBindKeyTo(binding, TEXT("Escape")));
    }

    // Left Shift and the middle mouse are map-fixed keys: the Shift-chord map opener refuses them, others may take them
    const int32 openMap = FmmBinding(TEXT("OpenMap"));
    TestFalse(TEXT("map opener refuses Left Shift"), CanBindKeyTo(openMap, TEXT("LeftShift")));
    TestFalse(TEXT("map opener refuses middle mouse"), CanBindKeyTo(openMap, TEXT("MiddleMouseButton")));
    TestTrue(TEXT("thrust up takes Left Shift"), CanBindKeyTo(FmmBinding(TEXT("ThrustUp")), TEXT("LeftShift")));
    TestTrue(TEXT("steer toggle takes middle mouse"),
        CanBindKeyTo(FmmBinding(TEXT("SteerToggle")), TEXT("MiddleMouseButton")));
    TestTrue(TEXT("steer hold takes a mouse button"),
        CanBindKeyTo(FmmBinding(TEXT("SteerHold")), TEXT("RightMouseButton")));

    // The Ctrl hotkeys are unaffected by the new layout
    TestTrue(TEXT("Ctrl+F1 menu"), IsMenuHotkey(TEXT("F1"), true));
    TestTrue(TEXT("Ctrl+F2 stats"), IsStatsHotkey(TEXT("F2"), true));
    TestEqual(TEXT("defaults need no overrides"), MakeOverrides(defaults).Num(), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathKeyMigrationDefaultsTest,
    "SOLTest.FlightModeMath.KeyMigrationAdoptsNewDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An old (schema 1 or 2) save with no overrides resolves to the new layout, with no shared keys
bool FSOLFlightModeMathKeyMigrationDefaultsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    const TArray<FString> keys = ResolveKeys(TMap<FString, FString>());
    TestTrue(TEXT("resolves to the defaults"), keys == DefaultKeys());

    struct FExpected
    {
        const TCHAR* Id;
        const TCHAR* Key;
    };
    const FExpected expected[] =
    {
        { TEXT("SteerHold"), TEXT("SpaceBar") },
        { TEXT("ThrustUp"), TEXT("LeftShift") },
        { TEXT("Boost"), TEXT("Tab") },
        { TEXT("ToggleAssist"), TEXT("C") },
        { TEXT("RecenterStick"), TEXT("B") },
        { TEXT("SteerToggle"), TEXT("MiddleMouseButton") },
    };
    for (const FExpected& entry : expected)
    {
        TestEqual(FString::Printf(TEXT("%s resolves to %s"), entry.Id, entry.Key), FmmKeyOf(keys, entry.Id),
            FString(entry.Key));
    }
    for (int32 binding = 0; binding < keys.Num(); ++binding)
    {
        TestFalse(FString::Printf(TEXT("%s unshared"), GetDef(binding).Id), HasConflict(keys, binding));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathKeyMigrationKeepsTest,
    "SOLTest.FlightModeMath.KeyMigrationKeepsOverrides",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An old save's explicit choices resolve as saved (id case-insensitive), invalid ones leave their binding at its
// default, and writing the resolved keys back yields exactly the valid overrides
bool FSOLFlightModeMathKeyMigrationKeepsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    TMap<FString, FString> saved;
    saved.Add(TEXT("Fire"), TEXT("K"));
    saved.Add(TEXT("boost"), TEXT("RightShift"));
    saved.Add(TEXT("ThrustUp"), TEXT("Home"));
    saved.Add(TEXT("NoSuchAction"), TEXT("K"));
    saved.Add(TEXT("DropTarget"), TEXT("Escape"));

    const TArray<FString> keys = ResolveKeys(saved);
    TestEqual(TEXT("fire kept"), FmmKeyOf(keys, TEXT("Fire")), FString(TEXT("K")));
    TestEqual(TEXT("boost kept (lower-case id)"), FmmKeyOf(keys, TEXT("Boost")), FString(TEXT("RightShift")));
    TestEqual(TEXT("thrust up kept"), FmmKeyOf(keys, TEXT("ThrustUp")), FString(TEXT("Home")));
    TestEqual(TEXT("reserved Escape ignored: default kept"), FmmKeyOf(keys, TEXT("DropTarget")), FString(TEXT("G")));
    TestEqual(TEXT("untouched binding takes its new default"), FmmKeyOf(keys, TEXT("ToggleAssist")),
        FString(TEXT("C")));
    TestEqual(TEXT("new binding takes its default"), FmmKeyOf(keys, TEXT("SteerHold")), FString(TEXT("SpaceBar")));

    // The unknown id binds nothing: every other binding still has its default
    const TArray<FString> defaults = DefaultKeys();
    for (int32 binding = 0; binding < keys.Num(); ++binding)
    {
        const FString id = GetDef(binding).Id;
        if (id != TEXT("Fire") && id != TEXT("Boost") && id != TEXT("ThrustUp"))
        {
            TestEqual(FString::Printf(TEXT("%s at its default"), *id), keys[binding], defaults[binding]);
        }
    }

    // Round trip: the valid overrides come back, the invalid ones are dropped
    TMap<FString, FString> valid;
    valid.Add(TEXT("Fire"), TEXT("K"));
    valid.Add(TEXT("Boost"), TEXT("RightShift"));
    valid.Add(TEXT("ThrustUp"), TEXT("Home"));
    TestTrue(TEXT("round trip keeps exactly the valid overrides"), FmmSameOverrides(MakeOverrides(keys), valid));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathKeyMigrationConflictsTest,
    "SOLTest.FlightModeMath.KeyMigrationFlagsConflicts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An old override on a key a new default now takes is kept and flagged on both rows, never silently moved
bool FSOLFlightModeMathKeyMigrationConflictsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    struct FCase
    {
        const TCHAR* OverrideId;    // the player's old rebind
        const TCHAR* Key;           // its key, now a new default of NewDefaultId
        const TCHAR* NewDefaultId;
    };
    const FCase cases[] =
    {
        { TEXT("ClearTarget"), TEXT("C"), TEXT("ToggleAssist") },
        { TEXT("Fire"), TEXT("SpaceBar"), TEXT("SteerHold") },
        { TEXT("FreeLook"), TEXT("MiddleMouseButton"), TEXT("SteerToggle") },
        { TEXT("DropTarget"), TEXT("Tab"), TEXT("Boost") },
        { TEXT("StrafeLeft"), TEXT("B"), TEXT("RecenterStick") },
        { TEXT("SurfaceLock"), TEXT("LeftShift"), TEXT("ThrustUp") },
    };
    for (const FCase& entry : cases)
    {
        TMap<FString, FString> saved;
        saved.Add(entry.OverrideId, entry.Key);
        TArray<FString> keys = ResolveKeys(saved);
        const int32 overridden = FmmBinding(entry.OverrideId);
        const int32 newDefault = FmmBinding(entry.NewDefaultId);
        TestEqual(FString::Printf(TEXT("%s keeps %s"), entry.OverrideId, entry.Key),
            FmmKeyOf(keys, entry.OverrideId), FString(entry.Key));
        TestEqual(FString::Printf(TEXT("%s takes %s"), entry.NewDefaultId, entry.Key),
            FmmKeyOf(keys, entry.NewDefaultId), FString(entry.Key));
        TestTrue(FString::Printf(TEXT("%s flagged"), entry.OverrideId), HasConflict(keys, overridden));
        TestTrue(FString::Printf(TEXT("%s flagged"), entry.NewDefaultId), HasConflict(keys, newDefault));
        TestEqual(FString::Printf(TEXT("%s conflicts with %s"), entry.NewDefaultId, entry.OverrideId),
            FindConflict(keys, newDefault, entry.Key), overridden);
        TestTrue(FString::Printf(TEXT("%s: conflicting override round-trips"), entry.OverrideId),
            FmmSameOverrides(MakeOverrides(keys), saved));

        // The player moves the new default to Insert (no default uses it; V is Resume's since issue #20): both clear
        int32 conflict = 123;
        TestTrue(FString::Printf(TEXT("%s rebinds"), entry.NewDefaultId),
            Rebind(keys, newDefault, TEXT("Insert"), conflict) == ERebindResult::Bound);
        TestEqual(FString::Printf(TEXT("%s: Insert free"), entry.NewDefaultId), conflict,
            static_cast<int32>(INDEX_NONE));
        TestFalse(FString::Printf(TEXT("%s clear"), entry.OverrideId), HasConflict(keys, overridden));
        TestFalse(FString::Printf(TEXT("%s clear"), entry.NewDefaultId), HasConflict(keys, newDefault));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathLayoutChangedTest, "SOLTest.FlightModeMath.LayoutChangedSince",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The one-time "default controls changed" notice applies only to real schema 1 to 3 saves (issue #20 changed the
// layout again)
bool FSOLFlightModeMathLayoutChangedTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    TestFalse(TEXT("-1"), DefaultLayoutChangedSince(-1));
    TestFalse(TEXT("0 (unversioned)"), DefaultLayoutChangedSince(0));
    TestTrue(TEXT("1"), DefaultLayoutChangedSince(1));
    TestTrue(TEXT("2"), DefaultLayoutChangedSince(2));
    TestTrue(TEXT("3 (issue #19 layout)"), DefaultLayoutChangedSince(FMM_LAYOUT_VERSION));
    TestFalse(TEXT("4 (issue #20 layout)"), DefaultLayoutChangedSince(FMM_ROUTE_LAYOUT_VERSION));
    TestFalse(TEXT("5"), DefaultLayoutChangedSince(FMM_ROUTE_LAYOUT_VERSION + 1));
    TestEqual(TEXT("layout version constant"), SOL::Settings::KEY_BINDING_LAYOUT_VERSION, FMM_ROUTE_LAYOUT_VERSION);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightModeMathSettingsSaveMigrationTest,
    "SOLTest.FlightModeMath.SettingsSaveMigratesKeys",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Schema 1 and 2 saves are migrated (never discarded) to the current version with their key overrides kept
bool FSOLFlightModeMathSettingsSaveMigrationTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("schema bumped for the new layout"), SOL::Settings::SAVE_VERSION >= FMM_ROUTE_LAYOUT_VERSION);
    TestTrue(TEXT("a v4 save is migrated"), SOLSettingsRules::ClassifySaveVersion(4, SOL::Settings::SAVE_VERSION)
        == SOLSettingsRules::ESaveVersionAction::Migrate);
    TestTrue(TEXT("a v3 save is migrated"), SOLSettingsRules::ClassifySaveVersion(3, SOL::Settings::SAVE_VERSION)
        == SOLSettingsRules::ESaveVersionAction::Migrate);
    TestTrue(TEXT("a v2 save is migrated"), SOLSettingsRules::ClassifySaveVersion(2, SOL::Settings::SAVE_VERSION)
        == SOLSettingsRules::ESaveVersionAction::Migrate);
    TestTrue(TEXT("a v1 save is migrated"), SOLSettingsRules::ClassifySaveVersion(1, SOL::Settings::SAVE_VERSION)
        == SOLSettingsRules::ESaveVersionAction::Migrate);

    // A schema 2 save: overrides (one now conflicting) and the other fields survive
    USOLSettingsSave* v2 = NewObject<USOLSettingsSave>();
    v2->Version = 2;
    v2->bShowOrbitLines = false;
    v2->MouseSensitivity = 2.0f;
    v2->bDefaultFlightAssist = false;
    v2->KeyOverrides.Add(TEXT("Fire"), TEXT("K"));
    v2->KeyOverrides.Add(TEXT("ClearTarget"), TEXT("C"));
    const TMap<FString, FString> v2Overrides = v2->KeyOverrides;
    v2->MigrateToCurrentVersion();
    TestEqual(TEXT("v2 -> current"), v2->Version, SOL::Settings::SAVE_VERSION);
    TestTrue(TEXT("v2 overrides kept"), FmmSameOverrides(v2->KeyOverrides, v2Overrides));
    TestFalse(TEXT("v2 hidden orbit lines kept (schema 2 had the field)"), v2->bShowOrbitLines);
    TestEqual(TEXT("v2 sensitivity kept"), v2->MouseSensitivity, 2.0f);
    TestFalse(TEXT("v2 assist-at-launch kept"), v2->bDefaultFlightAssist);

    // A schema 1 save goes through every step; its map toggle override moves to the jump-map opener (3 -> 4)
    USOLSettingsSave* v1 = NewObject<USOLSettingsSave>();
    v1->Version = 1;
    v1->bShowOrbitLines = false;
    v1->KeyOverrides.Add(TEXT("ToggleMap"), TEXT("Comma"));
    v1->MigrateToCurrentVersion();
    TestEqual(TEXT("v1 -> current"), v1->Version, SOL::Settings::SAVE_VERSION);
    TestTrue(TEXT("v1 orbit lines shown by the 1 -> 2 step"), v1->bShowOrbitLines);
    TestNull(TEXT("v1 ToggleMap override removed"), v1->KeyOverrides.Find(TEXT("ToggleMap")));
    const FString* mapKey = v1->KeyOverrides.Find(TEXT("OpenMapJump"));
    TestTrue(TEXT("v1 override moved to OpenMapJump"), mapKey != nullptr && *mapKey == TEXT("Comma"));
    TestEqual(TEXT("v1 override count"), v1->KeyOverrides.Num(), 1);

    // A current save is untouched, and migrating again is a no-op
    USOLSettingsSave* current = NewObject<USOLSettingsSave>();
    current->Version = SOL::Settings::SAVE_VERSION;
    current->bShowOrbitLines = false;
    current->KeyOverrides.Add(TEXT("Boost"), TEXT("LeftShift"));
    current->MigrateToCurrentVersion();
    TestEqual(TEXT("current version kept"), current->Version, SOL::Settings::SAVE_VERSION);
    TestFalse(TEXT("current flag kept"), current->bShowOrbitLines);
    const FString* boostKey = current->KeyOverrides.Find(TEXT("Boost"));
    TestTrue(TEXT("current override kept"), boostKey != nullptr && *boostKey == TEXT("LeftShift"));
    v2->MigrateToCurrentVersion();
    TestTrue(TEXT("second migrate keeps the overrides"), FmmSameOverrides(v2->KeyOverrides, v2Overrides));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
