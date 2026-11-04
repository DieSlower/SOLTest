/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Game/SOLInputHelpers.h"

#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputTriggers.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    // The actions of one test context: two plain O mappings, two chorded O mappings and an unrelated plain key
    struct FInputHelpersFixture
    {
        UInputMappingContext* Context = nullptr;
        UInputAction* PlainA = nullptr;
        UInputAction* PlainB = nullptr;
        UInputAction* AltO = nullptr;
        UInputAction* RightShiftO = nullptr;
        UInputAction* PlainP = nullptr;
        UInputAction* AltChord = nullptr;
        UInputAction* RightShiftChord = nullptr;
        UInputAction* ShiftChord = nullptr;
    };

    //////////////////////////////////////////////////////////////////////////
    // Builds a bare context: plain O, Alt+O, plain O (a second action), RShift+O, then plain P
    FInputHelpersFixture MakeInputHelpersFixture()
    {
        UObject* outer = GetTransientPackage();
        FInputHelpersFixture fixture;
        fixture.Context = NewObject<UInputMappingContext>(outer);
        fixture.PlainA = SOLInput::CreateAction(outer, TEXT("InputHelpersPlainA"), EInputActionValueType::Boolean);
        fixture.PlainB = SOLInput::CreateAction(outer, TEXT("InputHelpersPlainB"), EInputActionValueType::Boolean);
        fixture.AltO = SOLInput::CreateAction(outer, TEXT("InputHelpersAltO"), EInputActionValueType::Boolean);
        fixture.RightShiftO = SOLInput::CreateAction(outer, TEXT("InputHelpersRShiftO"),
            EInputActionValueType::Boolean);
        fixture.PlainP = SOLInput::CreateAction(outer, TEXT("InputHelpersPlainP"), EInputActionValueType::Boolean);
        fixture.AltChord = SOLInput::CreateAction(outer, TEXT("InputHelpersAlt"), EInputActionValueType::Boolean);
        fixture.RightShiftChord = SOLInput::CreateAction(outer, TEXT("InputHelpersRShift"),
            EInputActionValueType::Boolean);
        fixture.ShiftChord = SOLInput::CreateAction(outer, TEXT("InputHelpersShift"), EInputActionValueType::Boolean);
        SOLInput::MapPressedKey(fixture.Context, fixture.PlainA, EKeys::O, outer);
        SOLInput::MapChordedKey(fixture.Context, fixture.AltO, EKeys::O, fixture.AltChord, outer);
        SOLInput::MapPressedKey(fixture.Context, fixture.PlainB, EKeys::O, outer);
        SOLInput::MapChordedKey(fixture.Context, fixture.RightShiftO, EKeys::O, fixture.RightShiftChord, outer);
        SOLInput::MapPressedKey(fixture.Context, fixture.PlainP, EKeys::P, outer);
        return fixture;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the index of an action's mapping in the context, or INDEX_NONE
    int32 FindMappingIndex(const UInputMappingContext& context, const UInputAction* action)
    {
        return context.GetMappings().IndexOfByPredicate([action](const FEnhancedActionKeyMapping& mapping)
        {
            return mapping.Action == action;
        });
    }

    //////////////////////////////////////////////////////////////////////////
    // Counts a mapping's chord blockers on one chord action
    int32 CountBlockers(const FEnhancedActionKeyMapping& mapping, const UInputAction* chordAction)
    {
        int32 count = 0;
        for (const UInputTrigger* trigger : mapping.Triggers)
        {
            const UInputTriggerChordBlocker* blocker = Cast<const UInputTriggerChordBlocker>(trigger);
            count += blocker != nullptr && blocker->ChordAction == chordAction ? 1 : 0;
        }
        return count;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a mapping still has a Pressed trigger (its own triggers are kept)
    bool HasPressedTrigger(const FEnhancedActionKeyMapping& mapping)
    {
        return mapping.Triggers.ContainsByPredicate([](const UInputTrigger* trigger)
        {
            return Cast<const UInputTriggerPressed>(trigger) != nullptr;
        });
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInputHelpersBlockOrderTest, "SOLTest.InputHelpers.BlockPlainMappingsUnderChord",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every blocked plain O mapping ends after every chorded O mapping with the Shift blocker; counts and other order kept
bool FSOLInputHelpersBlockOrderTest::RunTest(const FString& /*parameters*/)
{
    const FInputHelpersFixture fixture = MakeInputHelpersFixture();
    UInputMappingContext& context = *fixture.Context;
    const int32 countBefore = context.GetMappings().Num();
    const int32 changed = SOLInput::BlockPlainMappingsUnderChord(&context, { EKeys::O }, fixture.ShiftChord,
        GetTransientPackage());
    TestEqual(TEXT("both plain O mappings are blocked"), changed, 2);
    TestEqual(TEXT("mapping count unchanged"), context.GetMappings().Num(), countBefore);

    // Order: each blocked plain O mapping after each chorded O mapping; the kept mappings keep their order
    const int32 plainA = FindMappingIndex(context, fixture.PlainA);
    const int32 plainB = FindMappingIndex(context, fixture.PlainB);
    const int32 altO = FindMappingIndex(context, fixture.AltO);
    const int32 rightShiftO = FindMappingIndex(context, fixture.RightShiftO);
    const int32 plainP = FindMappingIndex(context, fixture.PlainP);
    TestTrue(TEXT("every mapping is still there"), plainA != INDEX_NONE && plainB != INDEX_NONE
        && altO != INDEX_NONE && rightShiftO != INDEX_NONE && plainP != INDEX_NONE);
    TestTrue(TEXT("plain A after Alt+O and RShift+O"), plainA > altO && plainA > rightShiftO);
    TestTrue(TEXT("plain B after Alt+O and RShift+O"), plainB > altO && plainB > rightShiftO);
    TestTrue(TEXT("the kept mappings keep their order"), altO < rightShiftO && rightShiftO < plainP);
    TestTrue(TEXT("the blocked mappings keep their order"), plainA < plainB);

    // Blockers: one Shift blocker on each blocked mapping, its own Pressed trigger kept; none elsewhere
    if (plainA != INDEX_NONE && plainB != INDEX_NONE && altO != INDEX_NONE && rightShiftO != INDEX_NONE
        && plainP != INDEX_NONE)
    {
        const TArray<FEnhancedActionKeyMapping>& mappings = context.GetMappings();
        TestEqual(TEXT("plain A has one Shift blocker"), CountBlockers(mappings[plainA], fixture.ShiftChord), 1);
        TestEqual(TEXT("plain B has one Shift blocker"), CountBlockers(mappings[plainB], fixture.ShiftChord), 1);
        TestTrue(TEXT("plain A keeps its Pressed trigger"), HasPressedTrigger(mappings[plainA]));
        TestTrue(TEXT("plain B keeps its Pressed trigger"), HasPressedTrigger(mappings[plainB]));
        TestTrue(TEXT("plain A stays on O"), mappings[plainA].Key == EKeys::O);
        TestTrue(TEXT("plain B stays on O"), mappings[plainB].Key == EKeys::O);
        TestEqual(TEXT("Alt+O has no Shift blocker"), CountBlockers(mappings[altO], fixture.ShiftChord), 0);
        TestEqual(TEXT("RShift+O has no Shift blocker"), CountBlockers(mappings[rightShiftO], fixture.ShiftChord), 0);
        TestEqual(TEXT("plain P (another key) has no blocker"), CountBlockers(mappings[plainP], fixture.ShiftChord),
            0);
        TestEqual(TEXT("Alt+O keeps its two triggers"), mappings[altO].Triggers.Num(), 2);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInputHelpersBlockTwiceTest, "SOLTest.InputHelpers.BlockTwiceNoDuplicate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A second call changes nothing: no duplicated mapping or blocker, the order stays
bool FSOLInputHelpersBlockTwiceTest::RunTest(const FString& /*parameters*/)
{
    const FInputHelpersFixture fixture = MakeInputHelpersFixture();
    UInputMappingContext& context = *fixture.Context;
    const int32 countBefore = context.GetMappings().Num();
    SOLInput::BlockPlainMappingsUnderChord(&context, { EKeys::O }, fixture.ShiftChord, GetTransientPackage());
    TArray<const UInputAction*> orderAfterFirst;
    for (const FEnhancedActionKeyMapping& mapping : context.GetMappings())
    {
        orderAfterFirst.Add(mapping.Action);
    }
    const int32 changed = SOLInput::BlockPlainMappingsUnderChord(&context, { EKeys::O }, fixture.ShiftChord,
        GetTransientPackage());
    TestEqual(TEXT("the second call blocks nothing"), changed, 0);
    TestEqual(TEXT("mapping count unchanged"), context.GetMappings().Num(), countBefore);
    TArray<const UInputAction*> orderAfterSecond;
    for (const FEnhancedActionKeyMapping& mapping : context.GetMappings())
    {
        orderAfterSecond.Add(mapping.Action);
    }
    TestTrue(TEXT("order unchanged by the second call"), orderAfterFirst == orderAfterSecond);
    const int32 plainA = FindMappingIndex(context, fixture.PlainA);
    const int32 plainB = FindMappingIndex(context, fixture.PlainB);
    if (plainA != INDEX_NONE && plainB != INDEX_NONE)
    {
        TestEqual(TEXT("plain A still has one Shift blocker"),
            CountBlockers(context.GetMappings()[plainA], fixture.ShiftChord), 1);
        TestEqual(TEXT("plain B still has one Shift blocker"),
            CountBlockers(context.GetMappings()[plainB], fixture.ShiftChord), 1);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLInputHelpersBlockGuardsTest, "SOLTest.InputHelpers.BlockGuards",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// No context, no blocker action, no keys or no matching plain mapping: nothing changes and 0 is returned
bool FSOLInputHelpersBlockGuardsTest::RunTest(const FString& /*parameters*/)
{
    const FInputHelpersFixture fixture = MakeInputHelpersFixture();
    UInputMappingContext& context = *fixture.Context;
    const int32 countBefore = context.GetMappings().Num();
    UObject* outer = GetTransientPackage();
    TestEqual(TEXT("no context"), SOLInput::BlockPlainMappingsUnderChord(nullptr, { EKeys::O }, fixture.ShiftChord,
        outer), 0);
    TestEqual(TEXT("no blocker action"), SOLInput::BlockPlainMappingsUnderChord(&context, { EKeys::O }, nullptr,
        outer), 0);
    TestEqual(TEXT("no keys"), SOLInput::BlockPlainMappingsUnderChord(&context, {}, fixture.ShiftChord, outer), 0);
    TestEqual(TEXT("no plain mapping of the key"), SOLInput::BlockPlainMappingsUnderChord(&context, { EKeys::Q },
        fixture.ShiftChord, outer), 0);
    TestEqual(TEXT("mapping count unchanged"), context.GetMappings().Num(), countBefore);
    TestEqual(TEXT("plain A not blocked"), CountBlockers(context.GetMappings()[FindMappingIndex(context,
        fixture.PlainA)], fixture.ShiftChord), 0);
    TestEqual(TEXT("plain A still first"), FindMappingIndex(context, fixture.PlainA), 0);
    return true;
}

#endif
