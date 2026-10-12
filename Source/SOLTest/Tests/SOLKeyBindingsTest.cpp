/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuContent.h"
#include "Menu/SOLMenuText.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    constexpr int32 KEYBINDINGS_FIRE = static_cast<int32>(SOLKeyBindings::EBinding::Fire);
    constexpr int32 KEYBINDINGS_DROP = static_cast<int32>(SOLKeyBindings::EBinding::DropTarget);
    constexpr int32 KEYBINDINGS_BOOST = static_cast<int32>(SOLKeyBindings::EBinding::Boost);
    constexpr int32 KEYBINDINGS_FORWARD = static_cast<int32>(SOLKeyBindings::EBinding::ThrustForward);
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyBindingsTableTest, "SOLTest.KeyBindings.Table",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every binding has a unique id, a label and a default key that is valid, bindable and not shared
bool FSOLKeyBindingsTableTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    const TArray<FString> defaults = DefaultKeys();
    TestEqual(TEXT("one default per binding"), defaults.Num(), BINDING_COUNT);
    for (int32 binding = 0; binding < BINDING_COUNT; ++binding)
    {
        const FBindingDef& def = GetDef(binding);
        TestEqual(FString::Printf(TEXT("%s found by its id"), def.Id), FindById(def.Id), binding);
        TestEqual(FString::Printf(TEXT("%s found case-insensitively"), def.Id), FindById(FString(def.Id).ToLower()),
            binding);
        TestFalse(FString::Printf(TEXT("%s has a label"), def.Id), FString(def.Label).IsEmpty());
        TestTrue(FString::Printf(TEXT("%s default is bindable"), def.Id), CanBindKey(def.DefaultKey));
        TestTrue(FString::Printf(TEXT("%s default is an engine key"), def.Id), FKey(def.DefaultKey).IsValid());
        TestFalse(FString::Printf(TEXT("%s default is not shared"), def.Id), HasConflict(defaults, binding));
    }
    TestEqual(TEXT("fire defaults to the left mouse"), defaults[KEYBINDINGS_FIRE], FString(TEXT("LeftMouseButton")));
    TestEqual(TEXT("unknown id"), FindById(TEXT("NoSuchAction")), INDEX_NONE);
    TestEqual(TEXT("empty id"), FindById(FString()), INDEX_NONE);
    TestEqual(TEXT("GetDef clamps a negative index"), FString(GetDef(-5).Id), FString(GetDef(0).Id));
    TestEqual(TEXT("GetDef clamps a large index"), FString(GetDef(BINDING_COUNT + 3).Id),
        FString(GetDef(BINDING_COUNT - 1).Id));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyBindingsBindableTest, "SOLTest.KeyBindings.Bindable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Escape, the mouse axes, the wheel and the reserved P cannot be taken; ordinary keys and mouse buttons can
bool FSOLKeyBindingsBindableTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    const TCHAR* refused[] = { TEXT("Escape"), TEXT("escape"), TEXT(""), TEXT("Mouse2D"), TEXT("MouseX"),
        TEXT("MouseY"), TEXT("MouseWheelAxis"), TEXT("MouseScrollUp"), TEXT("MouseScrollDown"), TEXT("P"), TEXT("p") };
    for (const TCHAR* key : refused)
    {
        TestFalse(FString::Printf(TEXT("'%s' refused"), key), CanBindKey(key));
    }
    TestTrue(TEXT("Escape reserved"), IsReservedKey(TEXT("Escape")));
    TestFalse(TEXT("K not reserved"), IsReservedKey(TEXT("K")));
    const TCHAR* accepted[] = { TEXT("K"), TEXT("LeftMouseButton"), TEXT("RightMouseButton"), TEXT("ThumbMouseButton"),
        TEXT("F5"), TEXT("SpaceBar"), TEXT("LeftShift") };
    for (const TCHAR* key : accepted)
    {
        TestTrue(FString::Printf(TEXT("'%s' accepted"), key), CanBindKey(key));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyBindingsOverridesTest, "SOLTest.KeyBindings.Overrides",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Overrides only hold what differs from the defaults and resolve back to the same keys; bad entries are ignored
bool FSOLKeyBindingsOverridesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    TestEqual(TEXT("defaults need no overrides"), MakeOverrides(DefaultKeys()).Num(), 0);
    TestTrue(TEXT("no overrides resolve to the defaults"), ResolveKeys({}) == DefaultKeys());

    TArray<FString> keys = DefaultKeys();
    keys[KEYBINDINGS_FIRE] = TEXT("K");
    keys[KEYBINDINGS_FORWARD] = TEXT("Up");
    const TMap<FString, FString> overrides = MakeOverrides(keys);
    TestEqual(TEXT("two overrides"), overrides.Num(), 2);
    TestEqual(TEXT("fire override by id"), overrides.FindRef(TEXT("Fire")), FString(TEXT("K")));
    TestTrue(TEXT("round trip"), ResolveKeys(overrides) == keys);

    // A key equal to its default in another case is not an override
    TArray<FString> caseOnly = DefaultKeys();
    caseOnly[KEYBINDINGS_DROP] = TEXT("g");
    TestEqual(TEXT("case-only difference is not an override"), MakeOverrides(caseOnly).Num(), 0);

    // Unknown ids, unbindable keys and empty keys in a save are ignored (the default stays)
    TMap<FString, FString> bad;
    bad.Add(TEXT("NoSuchAction"), TEXT("K"));
    bad.Add(TEXT("Fire"), TEXT("Escape"));
    bad.Add(TEXT("DropTarget"), TEXT(""));
    bad.Add(TEXT("boost"), TEXT("B"));
    const TArray<FString> resolved = ResolveKeys(bad);
    TestEqual(TEXT("Escape override ignored"), resolved[KEYBINDINGS_FIRE], FString(TEXT("LeftMouseButton")));
    TestEqual(TEXT("empty override ignored"), resolved[KEYBINDINGS_DROP], FString(TEXT("G")));
    TestEqual(TEXT("lower-case id still applies"), resolved[KEYBINDINGS_BOOST], FString(TEXT("B")));

    // A short key array (older data) only yields overrides for what it has
    TArray<FString> shortKeys;
    shortKeys.Add(TEXT("Up"));
    TestEqual(TEXT("short array"), MakeOverrides(shortKeys).Num(), 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyBindingsRebindTest, "SOLTest.KeyBindings.Rebind",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Rebind binds, reports conflicts without blocking, refuses fixed keys and leaves the array alone when refused
bool FSOLKeyBindingsRebindTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    TArray<FString> keys = DefaultKeys();
    int32 conflict = 123;

    TestTrue(TEXT("free key binds"), Rebind(keys, KEYBINDINGS_FIRE, TEXT("K"), conflict) == ERebindResult::Bound);
    TestEqual(TEXT("no conflict"), conflict, INDEX_NONE);
    TestEqual(TEXT("fire is K"), keys[KEYBINDINGS_FIRE], FString(TEXT("K")));

    TestTrue(TEXT("same key again"), Rebind(keys, KEYBINDINGS_FIRE, TEXT("k"), conflict) == ERebindResult::Unchanged);

    // G is the drop-target key: boost takes it too and both are flagged
    TestTrue(TEXT("used key binds"), Rebind(keys, KEYBINDINGS_BOOST, TEXT("G"), conflict) == ERebindResult::Bound);
    TestEqual(TEXT("conflict reported"), conflict, KEYBINDINGS_DROP);
    TestTrue(TEXT("boost flagged"), HasConflict(keys, KEYBINDINGS_BOOST));
    TestTrue(TEXT("drop flagged"), HasConflict(keys, KEYBINDINGS_DROP));
    TestEqual(TEXT("FindConflict from the other side"), FindConflict(keys, KEYBINDINGS_DROP, TEXT("g")),
        KEYBINDINGS_BOOST);

    // Moving boost away clears both flags
    TestTrue(TEXT("boost back"), Rebind(keys, KEYBINDINGS_BOOST, TEXT("LeftShift"), conflict) == ERebindResult::Bound);
    TestFalse(TEXT("boost clear"), HasConflict(keys, KEYBINDINGS_BOOST));
    TestFalse(TEXT("drop clear"), HasConflict(keys, KEYBINDINGS_DROP));

    // Refusals change nothing
    const TArray<FString> before = keys;
    TestTrue(TEXT("Escape refused"), Rebind(keys, KEYBINDINGS_FIRE, TEXT("Escape"), conflict)
        == ERebindResult::Refused);
    TestEqual(TEXT("refusal reports no conflict"), conflict, INDEX_NONE);
    TestTrue(TEXT("reserved P refused"), Rebind(keys, KEYBINDINGS_FIRE, TEXT("P"), conflict) == ERebindResult::Refused);
    TestTrue(TEXT("bad index refused"), Rebind(keys, BINDING_COUNT, TEXT("K"), conflict) == ERebindResult::Refused);
    TestTrue(TEXT("negative index refused"), Rebind(keys, -1, TEXT("K"), conflict) == ERebindResult::Refused);
    TArray<FString> empty;
    TestTrue(TEXT("empty array refused"), Rebind(empty, KEYBINDINGS_FIRE, TEXT("K"), conflict)
        == ERebindResult::Refused);
    TestTrue(TEXT("refusals left the keys alone"), keys == before);

    TestEqual(TEXT("an empty key has no conflict"), FindConflict(keys, KEYBINDINGS_FIRE, FString()), INDEX_NONE);
    TestFalse(TEXT("HasConflict out of range"), HasConflict(keys, BINDING_COUNT));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyBindingsControlsCellTest, "SOLTest.KeyBindings.ControlsCell",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The Controls page's key cells show live keys for '@' ids and fixed chords otherwise
bool FSOLKeyBindingsControlsCellTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    TArray<FString> keys = DefaultKeys();
    TestEqual(TEXT("default fire"), SOLMenuContent::FormatBindings(TEXT("@Fire"), keys), FString(TEXT("LMB")));
    keys[KEYBINDINGS_FIRE] = TEXT("K");
    TestEqual(TEXT("rebound fire"), SOLMenuContent::FormatBindings(TEXT("@Fire"), keys), FString(TEXT("K")));
    TestEqual(TEXT("pair of ids"), SOLMenuContent::FormatBindings(TEXT("@ThrustForward|@ThrustBackward"), keys),
        FString(TEXT("W / S")));
    TestEqual(TEXT("mixed id and chord"), SOLMenuContent::FormatBindings(TEXT("@Fire|LeftShift+RightMouseButton"),
        keys), FString(TEXT("K / Shift+RMB")));
    TestEqual(TEXT("fixed key"), SOLMenuContent::FormatBindings(TEXT("Mouse2D"), keys), FString(TEXT("Mouse")));
    TestEqual(TEXT("unknown id reads unbound"), SOLMenuContent::FormatBindings(TEXT("@Nope"), keys),
        FString(TEXT("Unbound")));
    TestEqual(TEXT("short key array reads unbound"), SOLMenuContent::FormatBindings(TEXT("@Fire"), TArray<FString>()),
        FString(TEXT("Unbound")));
    TestEqual(TEXT("default overload"), SOLMenuContent::FormatBindings(TEXT("@Fire")), FString(TEXT("LMB")));

    // Every '@' id in the Controls table names a real binding, and every binding appears in the table once
    TArray<int32> seen;
    seen.Init(0, BINDING_COUNT);
    for (const SOLMenuText::FControlRow& row : SOLMenuText::CONTROL_ROWS)
    {
        TArray<FString> alternatives;
        FString(row.Bindings).ParseIntoArray(alternatives, SOLMenuText::BINDING_ALTERNATIVE_DELIMITER);
        for (const FString& alternative : alternatives)
        {
            if (alternative.StartsWith(TEXT("@")))
            {
                const int32 binding = FindById(alternative.RightChop(1));
                TestTrue(FString::Printf(TEXT("%s is a binding"), *alternative), binding != INDEX_NONE);
                if (binding != INDEX_NONE)
                {
                    ++seen[binding];
                }
            }
        }
    }
    for (int32 binding = 0; binding < BINDING_COUNT; ++binding)
    {
        TestEqual(FString::Printf(TEXT("%s shown once on the Controls page"), GetDef(binding).Id), seen[binding], 1);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyBindingsMapToggleTest, "SOLTest.KeyBindings.MapToggleKeys",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The map toggle (also mapped in the jump map's context) refuses the map's fixed keys; other actions may take them
bool FSOLKeyBindingsMapToggleTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    const int32 toggleMap = static_cast<int32>(EBinding::ToggleMap);
    const TCHAR* mapKeys[] = { TEXT("Enter"), TEXT("X"), TEXT("x"), TEXT("LeftMouseButton"), TEXT("RightMouseButton"),
        TEXT("MiddleMouseButton"), TEXT("LeftShift"), TEXT("RightShift") };
    for (const TCHAR* key : mapKeys)
    {
        TestTrue(FString::Printf(TEXT("'%s' is a map key"), key), IsMapFixedKey(key));
        TestFalse(FString::Printf(TEXT("map toggle refuses '%s'"), key), CanBindKeyTo(toggleMap, key));
        TestTrue(FString::Printf(TEXT("fire may take '%s'"), key), CanBindKeyTo(KEYBINDINGS_FIRE, key));
    }
    TestFalse(TEXT("J is not a map key"), IsMapFixedKey(TEXT("J")));
    TestTrue(TEXT("map toggle takes N"), CanBindKeyTo(toggleMap, TEXT("N")));
    TestFalse(TEXT("map toggle refuses Escape"), CanBindKeyTo(toggleMap, TEXT("Escape")));
    TestFalse(TEXT("out-of-range binding"), CanBindKeyTo(BINDING_COUNT, TEXT("N")));

    // Rebind refuses and leaves the key; a save override onto a map key is ignored
    TArray<FString> keys = DefaultKeys();
    int32 conflict = 0;
    TestTrue(TEXT("rebind map toggle to Enter refused"), Rebind(keys, toggleMap, TEXT("Enter"), conflict)
        == ERebindResult::Refused);
    TestEqual(TEXT("map toggle stays J"), keys[toggleMap], FString(TEXT("J")));
    TestTrue(TEXT("rebind map toggle to N"), Rebind(keys, toggleMap, TEXT("N"), conflict) == ERebindResult::Bound);
    TMap<FString, FString> overrides;
    overrides.Add(TEXT("ToggleMap"), TEXT("LeftShift"));
    TestEqual(TEXT("override onto a map key ignored"), ResolveKeys(overrides)[toggleMap], FString(TEXT("J")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyBindingsInvalidNamesTest, "SOLTest.KeyBindings.InvalidKeyNames",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Key names the engine does not know, and axes, cannot be bound; a corrupt save falls back to the defaults
bool FSOLKeyBindingsInvalidNamesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    TestFalse(TEXT("unknown key name"), CanBindKey(TEXT("NotAKey")));
    TestFalse(TEXT("unknown key name is not pressable"), IsPressableKey(TEXT("NotAKey")));
    TestFalse(TEXT("empty name is not pressable"), IsPressableKey(FString()));
    TestFalse(TEXT("gamepad stick axis"), CanBindKey(TEXT("Gamepad_LeftX")));
    TestFalse(TEXT("2D axis"), IsPressableKey(TEXT("Gamepad_Left2D")));
    TestTrue(TEXT("gamepad button"), CanBindKey(TEXT("Gamepad_FaceButton_Bottom")));
    TestTrue(TEXT("plain key"), IsPressableKey(TEXT("K")));

    TMap<FString, FString> corrupt;
    corrupt.Add(TEXT("Fire"), TEXT("NotAKey"));
    corrupt.Add(TEXT("Boost"), TEXT("Gamepad_LeftX"));
    corrupt.Add(TEXT("DropTarget"), TEXT("H"));
    const TArray<FString> resolved = ResolveKeys(corrupt);
    TestEqual(TEXT("unknown name dropped"), resolved[KEYBINDINGS_FIRE], FString(TEXT("LeftMouseButton")));
    TestEqual(TEXT("axis dropped"), resolved[KEYBINDINGS_BOOST], FString(TEXT("LeftShift")));
    TestEqual(TEXT("valid entry kept"), resolved[KEYBINDINGS_DROP], FString(TEXT("H")));

    TArray<FString> keys = DefaultKeys();
    int32 conflict = 0;
    TestTrue(TEXT("rebind to an unknown name refused"), Rebind(keys, KEYBINDINGS_FIRE, TEXT("NotAKey"), conflict)
        == ERebindResult::Refused);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyBindingsFormatKeyTextTest, "SOLTest.KeyBindings.FormatKeyText",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// '{<BindingId>}' placeholders show each action's current key and follow rebinds; other text is untouched
bool FSOLKeyBindingsFormatKeyTextTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    TArray<FString> keys = DefaultKeys();
    TestEqual(TEXT("defaults"), FormatKeyText(TEXT("{SurfaceLock} lock, {Fire} fire, {ToggleMap} map"), keys),
        FString(TEXT("L lock, LMB fire, J map")));
    keys[static_cast<int32>(EBinding::SurfaceLock)] = TEXT("K");
    keys[KEYBINDINGS_FIRE] = TEXT("SpaceBar");
    TestEqual(TEXT("rebound"), FormatKeyText(TEXT("{SurfaceLock} lock, {Fire} fire"), keys),
        FString(TEXT("K lock, Space fire")));
    TestEqual(TEXT("no placeholders"), FormatKeyText(TEXT("plain text"), keys), FString(TEXT("plain text")));
    TestEqual(TEXT("short key array"), FormatKeyText(TEXT("{Fire}"), TArray<FString>()), FString(TEXT("Unbound")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyBindingsControlsActionsTest, "SOLTest.KeyBindings.ControlsActions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The Controls page's action texts carry the real drop distance and wheel notches, with no placeholder left
bool FSOLKeyBindingsControlsActionsTest::RunTest(const FString& /*parameters*/)
{
    bool bFoundDrop = false;
    bool bFoundNotches = false;
    for (const SOLMenuText::FControlRow& row : SOLMenuText::CONTROL_ROWS)
    {
        const FString action = SOLMenuContent::FormatControlAction(row.Action);
        TestFalse(FString::Printf(TEXT("'%s' has no placeholder left"), *action), action.Contains(TEXT("{")));
        bFoundDrop |= action.Contains(TEXT("practice target 200 m ahead"));
        bFoundNotches |= action.Contains(TEXT("(10 notches per factor of ten)"));
    }
    TestTrue(TEXT("drop distance from the constant"), bFoundDrop);
    TestTrue(TEXT("wheel notches from the flight constant"), bFoundNotches);
    return true;
}

#endif
