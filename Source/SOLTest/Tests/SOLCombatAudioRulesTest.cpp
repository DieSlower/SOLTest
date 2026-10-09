/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Combat/SOLCombatAudioRules.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLCombatAudioSoundForEventTest, "SOLTest.CombatAudio.SoundForEvent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every event type maps to its sound; a hit picks shield or hull by the shield state; an eviction is silent
bool FSOLCombatAudioSoundForEventTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLCombatAudioRules;
    TestTrue(TEXT("Fired -> shot"), SoundForEvent(ESOLCombatEventType::BoltFired, false) == ESOLCombatSound::Shot);
    TestTrue(TEXT("Hit, shielded -> shield hit"),
        SoundForEvent(ESOLCombatEventType::TargetHit, true) == ESOLCombatSound::ShieldHit);
    TestTrue(TEXT("Hit, unshielded -> hull hit"),
        SoundForEvent(ESOLCombatEventType::TargetHit, false) == ESOLCombatSound::HullHit);
    TestTrue(TEXT("Shield broken"),
        SoundForEvent(ESOLCombatEventType::ShieldBroken, false) == ESOLCombatSound::ShieldBreak);
    TestTrue(TEXT("Destroyed -> explosion"),
        SoundForEvent(ESOLCombatEventType::TargetDestroyed, false) == ESOLCombatSound::Explosion);
    TestTrue(TEXT("Absorbed -> spark"),
        SoundForEvent(ESOLCombatEventType::BoltAbsorbed, false) == ESOLCombatSound::Spark);
    TestTrue(TEXT("Dropped -> drop"),
        SoundForEvent(ESOLCombatEventType::TargetDropped, false) == ESOLCombatSound::TargetDrop);
    TestTrue(TEXT("Evicted -> none"),
        SoundForEvent(ESOLCombatEventType::TargetEvicted, true) == ESOLCombatSound::None);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLCombatAudioMayPlayTest, "SOLTest.CombatAudio.MayPlay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The minimum interval drops a repeat inside it, allows one at or past it, and never blocks a first or rewound play
bool FSOLCombatAudioMayPlayTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLCombatAudioRules;
    TestTrue(TEXT("Never played"), MayPlay(NEVER_PLAYED_S, 0.0, 0.1));
    TestFalse(TEXT("Same instant"), MayPlay(5.0, 5.0, 0.1));
    TestFalse(TEXT("Inside the interval"), MayPlay(5.0, 5.05, 0.1));
    TestTrue(TEXT("Exactly at the interval"), MayPlay(5.0, 5.25, 0.25));
    TestTrue(TEXT("Past the interval"), MayPlay(5.0, 5.2, 0.1));
    TestTrue(TEXT("Clock went backwards"), MayPlay(5.0, 1.0, 0.1));
    TestTrue(TEXT("Zero interval always plays"), MayPlay(5.0, 5.0, 0.0));

    // 8 shots a second at a 0.06 s interval: every shot plays, two in the same frame do not
    double last = NEVER_PLAYED_S;
    int32 played = 0;
    for (int32 shot = 0; shot < 8; ++shot)
    {
        const double now = shot * 0.125;
        if (MayPlay(last, now, 0.06))
        {
            last = now;
            ++played;
        }
        if (MayPlay(last, now, 0.06))
        {
            ++played;
        }
    }
    TestEqual(TEXT("One per shot at 8/s"), played, 8);
    return true;
}

#endif
