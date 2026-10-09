/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Combat/SOLDamageMath.h"

#include "Misc/AutomationTest.h"

#include <cmath>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // SDD 7 starting numbers (shield, health, regen delay), plus an illustrative regen rate, typed independently
    constexpr double DAMAGEMATH_MAX_SHIELD = 100.0;
    constexpr double DAMAGEMATH_MAX_HEALTH = 100.0;
    constexpr double DAMAGEMATH_REGEN_DELAY_S = 3.0;
    constexpr double DAMAGEMATH_REGEN_PER_S = 10.0;

    // Tight tolerance for small arithmetic
    constexpr double DAMAGEMATH_TIGHT_TOL = 1.0e-9;

    //////////////////////////////////////////////////////////////////////////
    // Returns a quiet NaN for the invalid-input cases
    double DamageMathNaN()
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every field of a equals the matching field of b exactly
    bool DamageMathStatesEqual(const SOLDamageMath::FDamageState& a, const SOLDamageMath::FDamageState& b)
    {
        return a.Shield == b.Shield && a.Health == b.Health && a.SecondsSinceLastHit == b.SecondsSinceLastHit &&
            a.bDead == b.bDead;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a state whose shield was just partly drained by a hit (SecondsSinceLastHit = 0)
    SOLDamageMath::FDamageState DamageMathHitState(const double shield)
    {
        return SOLDamageMath::ApplyDamage(SOLDamageMath::MakeState(DAMAGEMATH_MAX_SHIELD, DAMAGEMATH_MAX_HEALTH),
            DAMAGEMATH_MAX_SHIELD - shield);
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLDamageMathMakeStateTest, "SOLTest.DamageMath.MakeState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// MakeState stores shield and health, zeroes SecondsSinceLastHit, and is dead only when health <= 0
bool FSOLDamageMathMakeStateTest::RunTest(const FString& /*parameters*/)
{
    const SOLDamageMath::FDamageState state = SOLDamageMath::MakeState(DAMAGEMATH_MAX_SHIELD, DAMAGEMATH_MAX_HEALTH);
    TestEqual(TEXT("Shield stored"), state.Shield, DAMAGEMATH_MAX_SHIELD);
    TestEqual(TEXT("Health stored"), state.Health, DAMAGEMATH_MAX_HEALTH);
    TestEqual(TEXT("SecondsSinceLastHit starts at 0"), state.SecondsSinceLastHit, 0.0);
    TestFalse(TEXT("Positive health is alive"), state.bDead);

    TestTrue(TEXT("Zero health is dead"), SOLDamageMath::MakeState(50.0, 0.0).bDead);
    TestTrue(TEXT("Negative health is dead"), SOLDamageMath::MakeState(50.0, -1.0).bDead);
    TestFalse(TEXT("Zero shield with health is alive"), SOLDamageMath::MakeState(0.0, 1.0).bDead);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLDamageMathShieldFirstTest, "SOLTest.DamageMath.ShieldFirst",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Damage drains the shield first and overflows into health
bool FSOLDamageMathShieldFirstTest::RunTest(const FString& /*parameters*/)
{
    const SOLDamageMath::FDamageState full = SOLDamageMath::MakeState(DAMAGEMATH_MAX_SHIELD, DAMAGEMATH_MAX_HEALTH);

    const SOLDamageMath::FDamageState small = SOLDamageMath::ApplyDamage(full, 10.0);
    TestNearlyEqual(TEXT("10 damage: shield 90"), small.Shield, 90.0, DAMAGEMATH_TIGHT_TOL);
    TestNearlyEqual(TEXT("10 damage: health untouched"), small.Health, 100.0, DAMAGEMATH_TIGHT_TOL);
    TestFalse(TEXT("10 damage: alive"), small.bDead);

    const SOLDamageMath::FDamageState exact = SOLDamageMath::ApplyDamage(full, 100.0);
    TestNearlyEqual(TEXT("100 damage: shield 0"), exact.Shield, 0.0, DAMAGEMATH_TIGHT_TOL);
    TestNearlyEqual(TEXT("100 damage: health untouched"), exact.Health, 100.0, DAMAGEMATH_TIGHT_TOL);

    const SOLDamageMath::FDamageState overflow = SOLDamageMath::ApplyDamage(full, 150.0);
    TestNearlyEqual(TEXT("150 damage: shield 0"), overflow.Shield, 0.0, DAMAGEMATH_TIGHT_TOL);
    TestNearlyEqual(TEXT("150 damage: health 50"), overflow.Health, 50.0, DAMAGEMATH_TIGHT_TOL);
    TestFalse(TEXT("150 damage: alive"), overflow.bDead);

    const SOLDamageMath::FDamageState thin = SOLDamageMath::ApplyDamage(SOLDamageMath::MakeState(5.0, 100.0), 10.0);
    TestNearlyEqual(TEXT("Thin shield: shield 0"), thin.Shield, 0.0, DAMAGEMATH_TIGHT_TOL);
    TestNearlyEqual(TEXT("Thin shield: health 95"), thin.Health, 95.0, DAMAGEMATH_TIGHT_TOL);

    // Twenty 10-damage bolts (SDD 7 numbers) kill exactly on the twentieth
    SOLDamageMath::FDamageState state = full;
    for (int32 shot = 0; shot < 19; ++shot)
    {
        state = SOLDamageMath::ApplyDamage(state, 10.0);
    }
    TestFalse(TEXT("Alive after 19 bolts"), state.bDead);
    state = SOLDamageMath::ApplyDamage(state, 10.0);
    TestTrue(TEXT("Dead after 20 bolts"), state.bDead);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLDamageMathLethalTest, "SOLTest.DamageMath.Lethal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Lethal damage clamps health and shield at 0 and sets bDead, including infinite damage
bool FSOLDamageMathLethalTest::RunTest(const FString& /*parameters*/)
{
    const SOLDamageMath::FDamageState full = SOLDamageMath::MakeState(DAMAGEMATH_MAX_SHIELD, DAMAGEMATH_MAX_HEALTH);

    const SOLDamageMath::FDamageState exact = SOLDamageMath::ApplyDamage(full, 200.0);
    TestEqual(TEXT("Exactly lethal: health 0"), exact.Health, 0.0);
    TestTrue(TEXT("Exactly lethal: dead"), exact.bDead);

    const SOLDamageMath::FDamageState over = SOLDamageMath::ApplyDamage(full, 250.0);
    TestEqual(TEXT("Overkill: health clamped to 0"), over.Health, 0.0);
    TestEqual(TEXT("Overkill: shield clamped to 0"), over.Shield, 0.0);
    TestTrue(TEXT("Overkill: dead"), over.bDead);

    const SOLDamageMath::FDamageState inf = SOLDamageMath::ApplyDamage(full, std::numeric_limits<double>::infinity());
    TestEqual(TEXT("Infinite damage: health 0 (not NaN or -inf)"), inf.Health, 0.0);
    TestEqual(TEXT("Infinite damage: shield 0"), inf.Shield, 0.0);
    TestTrue(TEXT("Infinite damage: dead"), inf.bDead);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLDamageMathInvalidDamageTest, "SOLTest.DamageMath.InvalidDamage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Zero, negative and NaN damage leave the state completely unchanged (including the hit timer)
bool FSOLDamageMathInvalidDamageTest::RunTest(const FString& /*parameters*/)
{
    // A state whose hit timer has run, so a wrongful reset would be visible
    SOLDamageMath::FDamageState state = SOLDamageMath::MakeState(40.0, 70.0);
    state.SecondsSinceLastHit = 5.0;

    TestTrue(TEXT("Zero damage is a no-op"), DamageMathStatesEqual(SOLDamageMath::ApplyDamage(state, 0.0), state));
    TestTrue(TEXT("Negative damage is a no-op (no healing)"),
        DamageMathStatesEqual(SOLDamageMath::ApplyDamage(state, -25.0), state));
    TestTrue(TEXT("NaN damage is a no-op"),
        DamageMathStatesEqual(SOLDamageMath::ApplyDamage(state, DamageMathNaN()), state));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLDamageMathHitResetsTimerTest, "SOLTest.DamageMath.HitResetsTimer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A valid hit resets SecondsSinceLastHit to 0; a dead state ignores further damage entirely
bool FSOLDamageMathHitResetsTimerTest::RunTest(const FString& /*parameters*/)
{
    SOLDamageMath::FDamageState state = SOLDamageMath::MakeState(40.0, 70.0);
    state.SecondsSinceLastHit = 5.0;
    TestEqual(TEXT("Hit resets the timer"), SOLDamageMath::ApplyDamage(state, 1.0).SecondsSinceLastHit, 0.0);

    SOLDamageMath::FDamageState dead = SOLDamageMath::ApplyDamage(SOLDamageMath::MakeState(0.0, 10.0), 50.0);
    dead.SecondsSinceLastHit = 2.0;
    TestTrue(TEXT("Dead state stays unchanged on further damage"),
        DamageMathStatesEqual(SOLDamageMath::ApplyDamage(dead, 10.0), dead));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLDamageMathRegenDelayTest, "SOLTest.DamageMath.RegenDelay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The shield does not regenerate until the delay passes, then grows only for the time past it, never touching health
bool FSOLDamageMathRegenDelayTest::RunTest(const FString& /*parameters*/)
{
    SOLDamageMath::FDamageState state = DamageMathHitState(50.0);
    TestEqual(TEXT("Setup: shield 50"), state.Shield, 50.0);

    state = SOLDamageMath::TickRegen(state, 2.0, DAMAGEMATH_REGEN_DELAY_S, DAMAGEMATH_REGEN_PER_S,
        DAMAGEMATH_MAX_SHIELD);
    TestNearlyEqual(TEXT("2 s after hit: timer 2"), state.SecondsSinceLastHit, 2.0, DAMAGEMATH_TIGHT_TOL);
    TestNearlyEqual(TEXT("2 s after hit: no regen yet"), state.Shield, 50.0, DAMAGEMATH_TIGHT_TOL);

    // This 2 s tick crosses the 3 s delay: only 1 s of it counts
    state = SOLDamageMath::TickRegen(state, 2.0, DAMAGEMATH_REGEN_DELAY_S, DAMAGEMATH_REGEN_PER_S,
        DAMAGEMATH_MAX_SHIELD);
    TestNearlyEqual(TEXT("4 s after hit: timer 4"), state.SecondsSinceLastHit, 4.0, DAMAGEMATH_TIGHT_TOL);
    TestNearlyEqual(TEXT("4 s after hit: shield 60"), state.Shield, 60.0, DAMAGEMATH_TIGHT_TOL);

    state = SOLDamageMath::TickRegen(state, 1.0, DAMAGEMATH_REGEN_DELAY_S, DAMAGEMATH_REGEN_PER_S,
        DAMAGEMATH_MAX_SHIELD);
    TestNearlyEqual(TEXT("5 s after hit: shield 70"), state.Shield, 70.0, DAMAGEMATH_TIGHT_TOL);
    TestNearlyEqual(TEXT("Regen never touches health"), state.Health, DAMAGEMATH_MAX_HEALTH, DAMAGEMATH_TIGHT_TOL);

    // Exactly reaching the delay gives no regen (nothing past it yet)
    const SOLDamageMath::FDamageState boundary = SOLDamageMath::TickRegen(DamageMathHitState(50.0),
        DAMAGEMATH_REGEN_DELAY_S, DAMAGEMATH_REGEN_DELAY_S, DAMAGEMATH_REGEN_PER_S, DAMAGEMATH_MAX_SHIELD);
    TestNearlyEqual(TEXT("Tick landing exactly on the delay: no regen"), boundary.Shield, 50.0, DAMAGEMATH_TIGHT_TOL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLDamageMathRegenClampTest, "SOLTest.DamageMath.RegenClamp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Regen clamps at maxShield, and a fresh full-shield state stays full
bool FSOLDamageMathRegenClampTest::RunTest(const FString& /*parameters*/)
{
    const SOLDamageMath::FDamageState longTick = SOLDamageMath::TickRegen(DamageMathHitState(50.0), 100.0,
        DAMAGEMATH_REGEN_DELAY_S, DAMAGEMATH_REGEN_PER_S, DAMAGEMATH_MAX_SHIELD);
    TestNearlyEqual(TEXT("Long tick clamps at max shield"), longTick.Shield, DAMAGEMATH_MAX_SHIELD,
        DAMAGEMATH_TIGHT_TOL);

    const SOLDamageMath::FDamageState hugeRate = SOLDamageMath::TickRegen(DamageMathHitState(10.0), 4.0,
        DAMAGEMATH_REGEN_DELAY_S, 1.0e9, DAMAGEMATH_MAX_SHIELD);
    TestNearlyEqual(TEXT("Huge rate clamps at max shield"), hugeRate.Shield, DAMAGEMATH_MAX_SHIELD,
        DAMAGEMATH_TIGHT_TOL);

    const SOLDamageMath::FDamageState fresh = SOLDamageMath::TickRegen(
        SOLDamageMath::MakeState(DAMAGEMATH_MAX_SHIELD, DAMAGEMATH_MAX_HEALTH), 10.0, DAMAGEMATH_REGEN_DELAY_S,
        DAMAGEMATH_REGEN_PER_S, DAMAGEMATH_MAX_SHIELD);
    TestNearlyEqual(TEXT("Full shield stays at max"), fresh.Shield, DAMAGEMATH_MAX_SHIELD, DAMAGEMATH_TIGHT_TOL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLDamageMathRegenFrameStepTest, "SOLTest.DamageMath.RegenFrameStep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Regen is frame-rate independent: 600 ticks of 1/60 s match one 10 s tick
bool FSOLDamageMathRegenFrameStepTest::RunTest(const FString& /*parameters*/)
{
    constexpr double rate = 5.0;
    SOLDamageMath::FDamageState stepped = DamageMathHitState(50.0);
    for (int32 frame = 0; frame < 600; ++frame)
    {
        stepped = SOLDamageMath::TickRegen(stepped, 1.0 / 60.0, DAMAGEMATH_REGEN_DELAY_S, rate, DAMAGEMATH_MAX_SHIELD);
    }
    const SOLDamageMath::FDamageState single = SOLDamageMath::TickRegen(DamageMathHitState(50.0), 10.0,
        DAMAGEMATH_REGEN_DELAY_S, rate, DAMAGEMATH_MAX_SHIELD);

    TestNearlyEqual(TEXT("Single 10 s tick: shield 50 + 7 s * 5 = 85"), single.Shield, 85.0, DAMAGEMATH_TIGHT_TOL);
    TestNearlyEqual(TEXT("Stepped ticks match single tick"), stepped.Shield, single.Shield, 1.0e-6);
    TestNearlyEqual(TEXT("Stepped timer reaches 10 s"), stepped.SecondsSinceLastHit, 10.0, 1.0e-6);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLDamageMathRegenInvalidTest, "SOLTest.DamageMath.RegenInvalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Dead states never regenerate; zero, negative or NaN dt leaves the state unchanged
bool FSOLDamageMathRegenInvalidTest::RunTest(const FString& /*parameters*/)
{
    SOLDamageMath::FDamageState dead = SOLDamageMath::ApplyDamage(
        SOLDamageMath::MakeState(DAMAGEMATH_MAX_SHIELD, DAMAGEMATH_MAX_HEALTH), 500.0);
    const SOLDamageMath::FDamageState deadTicked = SOLDamageMath::TickRegen(dead, 60.0, DAMAGEMATH_REGEN_DELAY_S,
        DAMAGEMATH_REGEN_PER_S, DAMAGEMATH_MAX_SHIELD);
    TestEqual(TEXT("Dead: shield stays 0"), deadTicked.Shield, 0.0);
    TestEqual(TEXT("Dead: health stays 0"), deadTicked.Health, 0.0);
    TestTrue(TEXT("Dead: stays dead"), deadTicked.bDead);

    SOLDamageMath::FDamageState state = DamageMathHitState(50.0);
    state.SecondsSinceLastHit = 10.0;
    TestTrue(TEXT("dt 0 is a no-op"), DamageMathStatesEqual(SOLDamageMath::TickRegen(state, 0.0,
        DAMAGEMATH_REGEN_DELAY_S, DAMAGEMATH_REGEN_PER_S, DAMAGEMATH_MAX_SHIELD), state));
    TestTrue(TEXT("Negative dt is a no-op"), DamageMathStatesEqual(SOLDamageMath::TickRegen(state, -1.0,
        DAMAGEMATH_REGEN_DELAY_S, DAMAGEMATH_REGEN_PER_S, DAMAGEMATH_MAX_SHIELD), state));
    TestTrue(TEXT("NaN dt is a no-op"), DamageMathStatesEqual(SOLDamageMath::TickRegen(state, DamageMathNaN(),
        DAMAGEMATH_REGEN_DELAY_S, DAMAGEMATH_REGEN_PER_S, DAMAGEMATH_MAX_SHIELD), state));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLDamageMathOverkillTest, "SOLTest.DamageMath.Overkill",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// OverkillDamage is the damage past exactly zero health (shield counted first), and 0 when not lethal
bool FSOLDamageMathOverkillTest::RunTest(const FString& /*parameters*/)
{
    const SOLDamageMath::FDamageState full = SOLDamageMath::MakeState(DAMAGEMATH_MAX_SHIELD, DAMAGEMATH_MAX_HEALTH);
    TestNearlyEqual(TEXT("250 vs 100+100: overkill 50"), SOLDamageMath::OverkillDamage(full, 250.0), 50.0,
        DAMAGEMATH_TIGHT_TOL);
    TestNearlyEqual(TEXT("Exactly lethal 200: overkill 0"), SOLDamageMath::OverkillDamage(full, 200.0), 0.0,
        DAMAGEMATH_TIGHT_TOL);
    TestNearlyEqual(TEXT("Non-lethal 150: overkill 0"), SOLDamageMath::OverkillDamage(full, 150.0), 0.0,
        DAMAGEMATH_TIGHT_TOL);
    TestNearlyEqual(TEXT("No shield, 30 vs 10 health: overkill 20"),
        SOLDamageMath::OverkillDamage(SOLDamageMath::MakeState(0.0, 10.0), 30.0), 20.0, DAMAGEMATH_TIGHT_TOL);
    TestEqual(TEXT("Zero damage: overkill 0"), SOLDamageMath::OverkillDamage(full, 0.0), 0.0);
    TestEqual(TEXT("Negative damage: overkill 0"), SOLDamageMath::OverkillDamage(full, -10.0), 0.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLDamageMathDeterminismTest, "SOLTest.DamageMath.Determinism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The same sequence of hits and regen ticks produces bit-identical states
bool FSOLDamageMathDeterminismTest::RunTest(const FString& /*parameters*/)
{
    SOLDamageMath::FDamageState a = SOLDamageMath::MakeState(DAMAGEMATH_MAX_SHIELD, DAMAGEMATH_MAX_HEALTH);
    SOLDamageMath::FDamageState b = a;
    for (int32 step = 0; step < 50; ++step)
    {
        const double damage = 3.7 * static_cast<double>(step % 4);
        const double dt = 0.013 * static_cast<double>(1 + step % 7);
        a = SOLDamageMath::TickRegen(SOLDamageMath::ApplyDamage(a, damage), dt, DAMAGEMATH_REGEN_DELAY_S,
            DAMAGEMATH_REGEN_PER_S, DAMAGEMATH_MAX_SHIELD);
        b = SOLDamageMath::TickRegen(SOLDamageMath::ApplyDamage(b, damage), dt, DAMAGEMATH_REGEN_DELAY_S,
            DAMAGEMATH_REGEN_PER_S, DAMAGEMATH_MAX_SHIELD);
    }
    TestTrue(TEXT("Repeated sequences end bit-identical"), DamageMathStatesEqual(a, b));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
