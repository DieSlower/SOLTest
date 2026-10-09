/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Combat/SOLFireCadence.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Shots per second used by most cases (the gun's tuned rate)
    constexpr double FIRECADENCE_RATE = 8.0;

    // Per-step shot cap used by most cases
    constexpr int32 FIRECADENCE_MAX_SHOTS = 8;

    // Tolerance for time comparisons
    constexpr double FIRECADENCE_EPSILON = 1.0e-9;

    //////////////////////////////////////////////////////////////////////////
    // Returns a quiet NaN for the invalid-input cases
    double FireCadenceNaN()
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    //////////////////////////////////////////////////////////////////////////
    // Holds the trigger for totalS seconds in steps of dt and returns the number of shots fired
    int32 FireCadenceCountHeldShots(const double totalS, const double dt)
    {
        double cooldown = 0.0;
        double elapsed = 0.0;
        int32 shots = 0;
        while (elapsed < totalS - FIRECADENCE_EPSILON)
        {
            const double step = FMath::Min(dt, totalS - elapsed);
            const SOLFireCadence::FStep result = SOLFireCadence::Step(cooldown, step, FIRECADENCE_RATE, true,
                FIRECADENCE_MAX_SHOTS);
            shots += result.Shots;
            cooldown = result.CooldownS;
            elapsed += step;
        }
        return shots;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFireCadenceFirstPressTest, "SOLTest.FireCadence.FirstPressFires",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A ready gun fires at the start of the step its trigger is held, then waits one interval
bool FSOLFireCadenceFirstPressTest::RunTest(const FString& /*parameters*/)
{
    const double dt = 1.0 / 60.0;
    const SOLFireCadence::FStep result = SOLFireCadence::Step(0.0, dt, FIRECADENCE_RATE, true, FIRECADENCE_MAX_SHOTS);
    TestEqual(TEXT("One shot"), result.Shots, 1);
    TestNearlyEqual(TEXT("Fired at the start of the step"), result.FirstShotAgeS, dt, FIRECADENCE_EPSILON);
    TestNearlyEqual(TEXT("Cooldown is one interval minus the step"), result.CooldownS, 1.0 / FIRECADENCE_RATE - dt,
        FIRECADENCE_EPSILON);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFireCadenceFrameRateTest, "SOLTest.FireCadence.FrameRateIndependent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Holding for the same time fires the same number of shots at any frame rate
bool FSOLFireCadenceFrameRateTest::RunTest(const FString& /*parameters*/)
{
    // 10.06 s at 8/s: shots at 0, 0.125, ..., 10.0 = 81, away from any interval boundary
    const double totalS = 10.06;
    const int32 expected = 81;
    TestEqual(TEXT("144 fps"), FireCadenceCountHeldShots(totalS, 1.0 / 144.0), expected);
    TestEqual(TEXT("60 fps"), FireCadenceCountHeldShots(totalS, 1.0 / 60.0), expected);
    TestEqual(TEXT("7 fps"), FireCadenceCountHeldShots(totalS, 1.0 / 7.0), expected);
    TestEqual(TEXT("2 fps"), FireCadenceCountHeldShots(totalS, 0.5), expected);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFireCadenceShotAgesTest, "SOLTest.FireCadence.ShotAges",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A long step fires every shot due inside it, each one interval younger than the last
bool FSOLFireCadenceShotAgesTest::RunTest(const FString& /*parameters*/)
{
    const SOLFireCadence::FStep result = SOLFireCadence::Step(0.0, 0.5, FIRECADENCE_RATE, true, FIRECADENCE_MAX_SHOTS);
    TestEqual(TEXT("Five shots in 0.5 s (0, 0.125, 0.25, 0.375, 0.5)"), result.Shots, 5);
    TestNearlyEqual(TEXT("First shot age"), SOLFireCadence::ShotAgeS(result, 0, FIRECADENCE_RATE), 0.5,
        FIRECADENCE_EPSILON);
    TestNearlyEqual(TEXT("Third shot age"), SOLFireCadence::ShotAgeS(result, 2, FIRECADENCE_RATE), 0.25,
        FIRECADENCE_EPSILON);
    TestNearlyEqual(TEXT("Last shot age"), SOLFireCadence::ShotAgeS(result, 4, FIRECADENCE_RATE), 0.0,
        FIRECADENCE_EPSILON);
    TestNearlyEqual(TEXT("Cooldown after the step"), result.CooldownS, 0.125, FIRECADENCE_EPSILON);

    // A pending cooldown delays the first shot inside the step
    const SOLFireCadence::FStep delayed = SOLFireCadence::Step(0.05, 0.1, FIRECADENCE_RATE, true,
        FIRECADENCE_MAX_SHOTS);
    TestEqual(TEXT("Delayed: one shot"), delayed.Shots, 1);
    TestNearlyEqual(TEXT("Delayed: fired 0.05 s into the step"), delayed.FirstShotAgeS, 0.05, FIRECADENCE_EPSILON);
    TestNearlyEqual(TEXT("Delayed: cooldown"), delayed.CooldownS, 0.075, FIRECADENCE_EPSILON);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFireCadenceTriggerUpTest, "SOLTest.FireCadence.TriggerUp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With the trigger up nothing fires, the cooldown runs out to zero, and tapping cannot beat the rate
bool FSOLFireCadenceTriggerUpTest::RunTest(const FString& /*parameters*/)
{
    const SOLFireCadence::FStep partial = SOLFireCadence::Step(0.1, 0.05, FIRECADENCE_RATE, false,
        FIRECADENCE_MAX_SHOTS);
    TestEqual(TEXT("No shots with the trigger up"), partial.Shots, 0);
    TestNearlyEqual(TEXT("Cooldown counts down"), partial.CooldownS, 0.05, FIRECADENCE_EPSILON);
    const SOLFireCadence::FStep spent = SOLFireCadence::Step(0.01, 0.05, FIRECADENCE_RATE, false,
        FIRECADENCE_MAX_SHOTS);
    TestNearlyEqual(TEXT("Cooldown never goes below zero"), spent.CooldownS, 0.0, FIRECADENCE_EPSILON);

    // Pressing on every other 60 fps frame for 10 s cannot fire more than holding would
    double cooldown = 0.0;
    int32 shots = 0;
    for (int32 frame = 0; frame < 600; ++frame)
    {
        const SOLFireCadence::FStep result = SOLFireCadence::Step(cooldown, 1.0 / 60.0, FIRECADENCE_RATE,
            frame % 2 == 0, FIRECADENCE_MAX_SHOTS);
        shots += result.Shots;
        cooldown = result.CooldownS;
    }
    TestTrue(TEXT("Tapping fires at most the held rate"), shots <= 81);
    TestTrue(TEXT("Tapping still fires"), shots > 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFireCadenceCapAndInvalidTest, "SOLTest.FireCadence.CapAndInvalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The per-step cap limits shots without banking them, and invalid inputs fire nothing
bool FSOLFireCadenceCapAndInvalidTest::RunTest(const FString& /*parameters*/)
{
    const SOLFireCadence::FStep capped = SOLFireCadence::Step(0.0, 0.5, 100.0, true, 8);
    TestEqual(TEXT("Capped at 8 shots"), capped.Shots, 8);
    TestNearlyEqual(TEXT("Capped shots are not banked"), capped.CooldownS, 0.0, FIRECADENCE_EPSILON);

    TestEqual(TEXT("NaN dt fires nothing"),
        SOLFireCadence::Step(0.0, FireCadenceNaN(), FIRECADENCE_RATE, true, 8).Shots, 0);
    TestEqual(TEXT("Negative dt fires nothing"), SOLFireCadence::Step(0.0, -0.1, FIRECADENCE_RATE, true, 8).Shots, 0);
    TestEqual(TEXT("Zero rate fires nothing"), SOLFireCadence::Step(0.0, 0.1, 0.0, true, 8).Shots, 0);
    TestEqual(TEXT("NaN rate fires nothing"),
        SOLFireCadence::Step(0.0, 0.1, FireCadenceNaN(), true, 8).Shots, 0);
    TestEqual(TEXT("Zero cap fires nothing"), SOLFireCadence::Step(0.0, 0.1, FIRECADENCE_RATE, true, 0).Shots, 0);
    const SOLFireCadence::FStep nanCooldown = SOLFireCadence::Step(FireCadenceNaN(), 0.01,
        FIRECADENCE_RATE, true, 8);
    TestEqual(TEXT("NaN cooldown is treated as ready"), nanCooldown.Shots, 1);
    TestTrue(TEXT("Cooldown is finite"), FMath::IsFinite(nanCooldown.CooldownS));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
