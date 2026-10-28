/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLBindingLabel.h"
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
// Escape, the mouse axes and the wheel cannot be taken; ordinary keys, P (no longer reserved since issue #20) and mouse
// buttons can
bool FSOLKeyBindingsBindableTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    const TCHAR* refused[] = { TEXT("Escape"), TEXT("escape"), TEXT(""), TEXT("Mouse2D"), TEXT("MouseX"),
        TEXT("MouseY"), TEXT("MouseWheelAxis"), TEXT("MouseScrollUp"), TEXT("MouseScrollDown") };
    for (const TCHAR* key : refused)
    {
        TestFalse(FString::Printf(TEXT("'%s' refused"), key), CanBindKey(key));
    }
    TestTrue(TEXT("Escape reserved"), IsReservedKey(TEXT("Escape")));
    TestFalse(TEXT("K not reserved"), IsReservedKey(TEXT("K")));
    TestFalse(TEXT("P not reserved"), IsReservedKey(TEXT("P")));
    TestFalse(TEXT("p not reserved"), IsReservedKey(TEXT("p")));
    const TCHAR* accepted[] = { TEXT("K"), TEXT("LeftMouseButton"), TEXT("RightMouseButton"), TEXT("ThumbMouseButton"),
        TEXT("F5"), TEXT("SpaceBar"), TEXT("LeftShift"), TEXT("P") };
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
    bad.Add(TEXT("boost"), TEXT("K"));
    const TArray<FString> resolved = ResolveKeys(bad);
    TestEqual(TEXT("Escape override ignored"), resolved[KEYBINDINGS_FIRE], FString(TEXT("LeftMouseButton")));
    TestEqual(TEXT("empty override ignored"), resolved[KEYBINDINGS_DROP], FString(TEXT("G")));
    TestEqual(TEXT("lower-case id still applies"), resolved[KEYBINDINGS_BOOST], FString(TEXT("K")));
    TestFalse(TEXT("lower-case id override is not a conflict"), HasConflict(resolved, KEYBINDINGS_BOOST));

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
    TestTrue(TEXT("boost back"), Rebind(keys, KEYBINDINGS_BOOST, TEXT("Tab"), conflict) == ERebindResult::Bound);
    TestEqual(TEXT("boost back on its default reports no conflict"), conflict, INDEX_NONE);
    TestFalse(TEXT("boost clear"), HasConflict(keys, KEYBINDINGS_BOOST));
    TestFalse(TEXT("drop clear"), HasConflict(keys, KEYBINDINGS_DROP));

    // Refusals change nothing
    const TArray<FString> before = keys;
    TestTrue(TEXT("Escape refused"), Rebind(keys, KEYBINDINGS_FIRE, TEXT("Escape"), conflict)
        == ERebindResult::Refused);
    TestEqual(TEXT("refusal reports no conflict"), conflict, INDEX_NONE);
    TestTrue(TEXT("mouse axis refused"), Rebind(keys, KEYBINDINGS_FIRE, TEXT("MouseX"), conflict)
        == ERebindResult::Refused);
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
    TestEqual(TEXT("map opener cell is a Shift chord"), SOLMenuContent::FormatBindings(TEXT("@OpenMap"), keys),
        FString(TEXT("Shift+M")));

    // The issue #20 rows are listed and the removed map toggle is not
    bool bFoundOpenMap = false;
    bool bFoundOpenMapJump = false;
    bool bFoundResume = false;
    bool bFoundRouteOverlay = false;
    bool bFoundToggleMap = false;
    for (const SOLMenuText::FControlRow& row : SOLMenuText::CONTROL_ROWS)
    {
        TArray<FString> alternatives;
        FString(row.Bindings).ParseIntoArray(alternatives, SOLMenuText::BINDING_ALTERNATIVE_DELIMITER);
        bFoundOpenMap |= alternatives.Contains(TEXT("@OpenMap"));
        bFoundOpenMapJump |= alternatives.Contains(TEXT("@OpenMapJump"));
        bFoundResume |= alternatives.Contains(TEXT("@Resume"));
        bFoundRouteOverlay |= alternatives.Contains(TEXT("@ToggleRouteOverlay"));
        bFoundToggleMap |= alternatives.Contains(TEXT("@ToggleMap"));
    }
    TestTrue(TEXT("Controls lists @OpenMap"), bFoundOpenMap);
    TestTrue(TEXT("Controls lists @OpenMapJump"), bFoundOpenMapJump);
    TestTrue(TEXT("Controls lists @Resume"), bFoundResume);
    TestTrue(TEXT("Controls lists @ToggleRouteOverlay"), bFoundRouteOverlay);
    TestFalse(TEXT("Controls no longer lists @ToggleMap"), bFoundToggleMap);

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

// Issue #20 map and route bindings, written against the issue #20 contract (SDD 20) before the production code

namespace
{
    constexpr int32 KEYBINDINGS_RADAR_AUTO = static_cast<int32>(SOLKeyBindings::EBinding::RadarAuto);
    constexpr int32 KEYBINDINGS_OPEN_MAP = static_cast<int32>(SOLKeyBindings::EBinding::OpenMap);
    constexpr int32 KEYBINDINGS_OPEN_MAP_JUMP = static_cast<int32>(SOLKeyBindings::EBinding::OpenMapJump);
    constexpr int32 KEYBINDINGS_RESUME = static_cast<int32>(SOLKeyBindings::EBinding::Resume);
    constexpr int32 KEYBINDINGS_ROUTE_OVERLAY = static_cast<int32>(SOLKeyBindings::EBinding::ToggleRouteOverlay);
    constexpr int32 KEYBINDINGS_MATCH_LOCK = static_cast<int32>(SOLKeyBindings::EBinding::MatchLock);
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyBindingsRouteBindingsTest, "SOLTest.KeyBindings.RouteBindings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// ToggleMap is gone; OpenMap, OpenMapJump, Resume and ToggleRouteOverlay follow RadarAuto, with defaults and chords
bool FSOLKeyBindingsRouteBindingsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    TestEqual(TEXT("ToggleMap removed"), FindById(TEXT("ToggleMap")), INDEX_NONE);
    TestEqual(TEXT("OpenMap after RadarAuto"), KEYBINDINGS_OPEN_MAP, KEYBINDINGS_RADAR_AUTO + 1);
    TestEqual(TEXT("OpenMapJump next"), KEYBINDINGS_OPEN_MAP_JUMP, KEYBINDINGS_RADAR_AUTO + 2);
    TestEqual(TEXT("Resume next"), KEYBINDINGS_RESUME, KEYBINDINGS_RADAR_AUTO + 3);
    TestEqual(TEXT("ToggleRouteOverlay next"), KEYBINDINGS_ROUTE_OVERLAY, KEYBINDINGS_RADAR_AUTO + 4);
    TestEqual(TEXT("ToggleRouteOverlay is last"), KEYBINDINGS_ROUTE_OVERLAY, BINDING_COUNT - 1);

    struct FExpected
    {
        int32 Binding;
        const TCHAR* Id;
        const TCHAR* DefaultKey;
        bool bChord;
        const TCHAR* Area;
        const TCHAR* Label;
    };
    const FExpected expected[] =
    {
        { KEYBINDINGS_OPEN_MAP, TEXT("OpenMap"), TEXT("M"), true, TEXT("Map"), TEXT("Open or close the map") },
        { KEYBINDINGS_OPEN_MAP_JUMP, TEXT("OpenMapJump"), TEXT("J"), true, TEXT("Map"),
            TEXT("Open the map in Jump mode") },
        { KEYBINDINGS_RESUME, TEXT("Resume"), TEXT("V"), false, TEXT("Routes"), TEXT("Resume a paused route") },
        { KEYBINDINGS_ROUTE_OVERLAY, TEXT("ToggleRouteOverlay"), TEXT("P"), false, TEXT("Routes"),
            TEXT("Show or hide routes") },
    };
    const TArray<FString> defaults = DefaultKeys();
    for (const FExpected& entry : expected)
    {
        const FBindingDef& def = GetDef(entry.Binding);
        TestEqual(FString::Printf(TEXT("%s id"), entry.Id), FString(def.Id), FString(entry.Id));
        TestEqual(FString::Printf(TEXT("%s found by id"), entry.Id), FindById(entry.Id), entry.Binding);
        TestEqual(FString::Printf(TEXT("%s default"), entry.Id), FString(def.DefaultKey), FString(entry.DefaultKey));
        TestEqual(FString::Printf(TEXT("%s resolved default"), entry.Id), defaults[entry.Binding],
            FString(entry.DefaultKey));
        TestEqual(FString::Printf(TEXT("%s chord flag"), entry.Id), def.bShiftChord, entry.bChord);
        TestEqual(FString::Printf(TEXT("%s IsShiftChord"), entry.Id), IsShiftChord(entry.Binding), entry.bChord);
        TestEqual(FString::Printf(TEXT("%s area"), entry.Id), FString(def.Area), FString(entry.Area));
        TestEqual(FString::Printf(TEXT("%s label"), entry.Id), FString(def.Label), FString(entry.Label));
    }

    // Only the two map openers are chords
    for (int32 binding = 0; binding < BINDING_COUNT; ++binding)
    {
        const bool bExpectChord = binding == KEYBINDINGS_OPEN_MAP || binding == KEYBINDINGS_OPEN_MAP_JUMP;
        TestEqual(FString::Printf(TEXT("%s chord"), GetDef(binding).Id), IsShiftChord(binding), bExpectChord);
    }
    TestFalse(TEXT("IsShiftChord negative"), IsShiftChord(-1));
    TestFalse(TEXT("IsShiftChord past the end"), IsShiftChord(BINDING_COUNT));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyBindingsMapFixedKeysTest, "SOLTest.KeyBindings.MapFixedKeys",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Resume refuses every map fixed key, the Shift-chord openers refuse Shift and the mouse buttons, the rest only need
// CanBindKey
bool FSOLKeyBindingsMapFixedKeysTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    const TCHAR* mapKeys[] = { TEXT("LeftMouseButton"), TEXT("RightMouseButton"), TEXT("MiddleMouseButton"),
        TEXT("LeftShift"), TEXT("RightShift"), TEXT("X"), TEXT("x"), TEXT("Enter"), TEXT("Delete"), TEXT("J"),
        TEXT("j"), TEXT("M"), TEXT("m"), TEXT("P"), TEXT("p") };
    for (const TCHAR* key : mapKeys)
    {
        TestTrue(FString::Printf(TEXT("'%s' is a map key"), key), IsMapFixedKey(key));
        TestFalse(FString::Printf(TEXT("resume refuses '%s'"), key), CanBindKeyTo(KEYBINDINGS_RESUME, key));
        TestTrue(FString::Printf(TEXT("fire may take '%s'"), key), CanBindKeyTo(KEYBINDINGS_FIRE, key));
    }
    const TCHAR* notMapKeys[] = { TEXT("V"), TEXT("K"), TEXT("Comma") };
    for (const TCHAR* key : notMapKeys)
    {
        TestFalse(FString::Printf(TEXT("'%s' is not a map key"), key), IsMapFixedKey(key));
        TestTrue(FString::Printf(TEXT("resume takes '%s'"), key), CanBindKeyTo(KEYBINDINGS_RESUME, key));
    }
    TestTrue(TEXT("route overlay takes P"), CanBindKeyTo(KEYBINDINGS_ROUTE_OVERLAY, TEXT("P")));
    TestTrue(TEXT("route overlay takes X"), CanBindKeyTo(KEYBINDINGS_ROUTE_OVERLAY, TEXT("X")));

    // The Shift-chord openers refuse Shift and the mouse buttons, but take other map keys and plain keys
    const int32 openers[] = { KEYBINDINGS_OPEN_MAP, KEYBINDINGS_OPEN_MAP_JUMP };
    const TCHAR* chordRefused[] = { TEXT("LeftShift"), TEXT("RightShift"), TEXT("LeftMouseButton"),
        TEXT("RightMouseButton"), TEXT("MiddleMouseButton") };
    const TCHAR* chordAccepted[] = { TEXT("X"), TEXT("Enter"), TEXT("M"), TEXT("J"), TEXT("K") };
    for (const int32 opener : openers)
    {
        for (const TCHAR* key : chordRefused)
        {
            TestFalse(FString::Printf(TEXT("%s refuses '%s'"), GetDef(opener).Id, key), CanBindKeyTo(opener, key));
        }
        for (const TCHAR* key : chordAccepted)
        {
            TestTrue(FString::Printf(TEXT("%s takes '%s'"), GetDef(opener).Id, key), CanBindKeyTo(opener, key));
        }
        TestFalse(FString::Printf(TEXT("%s refuses Escape"), GetDef(opener).Id), CanBindKeyTo(opener, TEXT("Escape")));
    }
    TestFalse(TEXT("out-of-range binding"), CanBindKeyTo(BINDING_COUNT, TEXT("N")));

    // Rebind refuses and leaves the key; save overrides a binding cannot take are ignored
    TArray<FString> keys = DefaultKeys();
    int32 conflict = 0;
    TestTrue(TEXT("rebind resume to X refused"), Rebind(keys, KEYBINDINGS_RESUME, TEXT("X"), conflict)
        == ERebindResult::Refused);
    TestEqual(TEXT("resume stays V"), keys[KEYBINDINGS_RESUME], FString(TEXT("V")));
    TestTrue(TEXT("rebind map opener to LeftShift refused"), Rebind(keys, KEYBINDINGS_OPEN_MAP, TEXT("LeftShift"),
        conflict) == ERebindResult::Refused);
    TestEqual(TEXT("map opener stays M"), keys[KEYBINDINGS_OPEN_MAP], FString(TEXT("M")));
    TMap<FString, FString> overrides;
    overrides.Add(TEXT("Resume"), TEXT("X"));
    overrides.Add(TEXT("OpenMap"), TEXT("LeftShift"));
    overrides.Add(TEXT("ToggleRouteOverlay"), TEXT("K"));
    const TArray<FString> resolved = ResolveKeys(overrides);
    TestEqual(TEXT("resume override onto a map key ignored"), resolved[KEYBINDINGS_RESUME], FString(TEXT("V")));
    TestEqual(TEXT("chord override onto Shift ignored"), resolved[KEYBINDINGS_OPEN_MAP], FString(TEXT("M")));
    TestEqual(TEXT("valid overlay override applied"), resolved[KEYBINDINGS_ROUTE_OVERLAY], FString(TEXT("K")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyBindingsChordConflictTest, "SOLTest.KeyBindings.ChordConflicts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A Shift chord and a plain binding on one key do not conflict; two plain or two chord bindings on one key do
bool FSOLKeyBindingsChordConflictTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    TArray<FString> keys = DefaultKeys();
    TestEqual(TEXT("match lock defaults to M"), keys[KEYBINDINGS_MATCH_LOCK], FString(TEXT("M")));
    TestFalse(TEXT("Shift+M opener does not conflict with M"), HasConflict(keys, KEYBINDINGS_OPEN_MAP));
    TestFalse(TEXT("M match lock does not conflict with Shift+M"), HasConflict(keys, KEYBINDINGS_MATCH_LOCK));
    TestEqual(TEXT("FindConflict: plain M vs chord M"), FindConflict(keys, KEYBINDINGS_MATCH_LOCK, TEXT("M")),
        INDEX_NONE);
    TestEqual(TEXT("FindConflict: chord M vs plain M"), FindConflict(keys, KEYBINDINGS_OPEN_MAP, TEXT("m")),
        INDEX_NONE);

    // Two chords on one key conflict
    int32 conflict = 123;
    TestTrue(TEXT("jump opener to M binds"), Rebind(keys, KEYBINDINGS_OPEN_MAP_JUMP, TEXT("M"), conflict)
        == ERebindResult::Bound);
    TestEqual(TEXT("chord conflict reported"), conflict, KEYBINDINGS_OPEN_MAP);
    TestTrue(TEXT("opener flagged"), HasConflict(keys, KEYBINDINGS_OPEN_MAP));
    TestTrue(TEXT("jump opener flagged"), HasConflict(keys, KEYBINDINGS_OPEN_MAP_JUMP));
    TestFalse(TEXT("plain M still not flagged"), HasConflict(keys, KEYBINDINGS_MATCH_LOCK));
    TestTrue(TEXT("jump opener back to J"), Rebind(keys, KEYBINDINGS_OPEN_MAP_JUMP, TEXT("J"), conflict)
        == ERebindResult::Bound);
    TestEqual(TEXT("J free for a chord"), conflict, INDEX_NONE);

    // Two plain bindings on one key conflict; a plain binding on a chord's key does not
    TestTrue(TEXT("route overlay to M binds"), Rebind(keys, KEYBINDINGS_ROUTE_OVERLAY, TEXT("M"), conflict)
        == ERebindResult::Bound);
    TestEqual(TEXT("plain conflict reported"), conflict, KEYBINDINGS_MATCH_LOCK);
    TestTrue(TEXT("route overlay flagged"), HasConflict(keys, KEYBINDINGS_ROUTE_OVERLAY));
    TestTrue(TEXT("match lock flagged"), HasConflict(keys, KEYBINDINGS_MATCH_LOCK));
    TestFalse(TEXT("chord opener not flagged by plain M"), HasConflict(keys, KEYBINDINGS_OPEN_MAP));
    TestTrue(TEXT("fire to J binds"), Rebind(keys, KEYBINDINGS_FIRE, TEXT("J"), conflict) == ERebindResult::Bound);
    TestEqual(TEXT("plain J vs chord J: no conflict"), conflict, INDEX_NONE);
    TestFalse(TEXT("fire not flagged"), HasConflict(keys, KEYBINDINGS_FIRE));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyBindingsDisplayNameTest, "SOLTest.KeyBindings.BindingDisplayName",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A chord shows "Shift+<key>", a plain binding its key label, and a missing or empty entry "Unbound" with no prefix
bool FSOLKeyBindingsDisplayNameTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    TArray<FString> keys = DefaultKeys();
    TestEqual(TEXT("map opener"), BindingDisplayName(KEYBINDINGS_OPEN_MAP, keys), FString(TEXT("Shift+M")));
    TestEqual(TEXT("jump opener"), BindingDisplayName(KEYBINDINGS_OPEN_MAP_JUMP, keys), FString(TEXT("Shift+J")));
    TestEqual(TEXT("resume"), BindingDisplayName(KEYBINDINGS_RESUME, keys), FString(TEXT("V")));
    TestEqual(TEXT("route overlay"), BindingDisplayName(KEYBINDINGS_ROUTE_OVERLAY, keys), FString(TEXT("P")));
    TestEqual(TEXT("fire label"), BindingDisplayName(KEYBINDINGS_FIRE, keys), FString(TEXT("LMB")));

    keys[KEYBINDINGS_OPEN_MAP] = TEXT("Comma");
    TestEqual(TEXT("rebound opener"), BindingDisplayName(KEYBINDINGS_OPEN_MAP, keys),
        TEXT("Shift+") + SOLBindingLabel::KeyDisplayName(TEXT("Comma")));
    keys[KEYBINDINGS_OPEN_MAP] = FString();
    TestEqual(TEXT("empty chord entry reads unbound"), BindingDisplayName(KEYBINDINGS_OPEN_MAP, keys),
        SOLBindingLabel::KeyDisplayName(FString()));
    TestEqual(TEXT("missing chord entry reads unbound"), BindingDisplayName(KEYBINDINGS_OPEN_MAP, TArray<FString>()),
        SOLBindingLabel::KeyDisplayName(FString()));
    TestEqual(TEXT("unbound text"), SOLBindingLabel::KeyDisplayName(FString()), FString(TEXT("Unbound")));
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
    TestEqual(TEXT("axis dropped"), resolved[KEYBINDINGS_BOOST], FString(TEXT("Tab")));
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
    TestEqual(TEXT("defaults"), FormatKeyText(TEXT("{SurfaceLock} lock, {Fire} fire, {OpenMap} map"), keys),
        FString(TEXT("L lock, LMB fire, Shift+M map")));
    TestEqual(TEXT("chord and plain route keys"), FormatKeyText(TEXT("{OpenMapJump} jump, {Resume} resume"), keys),
        FString(TEXT("Shift+J jump, V resume")));
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

// Menu and stats hotkey tests (issue #11, Part 10e), written against the 10e contract in
// Docs/Plans/11-scale-and-performance-demo-plan.md before any production code existed.

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyBindingsMenuHotkeysTest, "SOLTest.KeyBindings.MenuHotkeys",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Ctrl+F1 is the menu hotkey and Ctrl+F2 the stats hotkey (key name case-insensitive); nothing else matches either
bool FSOLKeyBindingsMenuHotkeysTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    TestTrue(TEXT("Ctrl+F1 opens the menu"), IsMenuHotkey(TEXT("F1"), true));
    TestTrue(TEXT("Ctrl+f1 opens the menu"), IsMenuHotkey(TEXT("f1"), true));
    TestFalse(TEXT("F1 without Ctrl"), IsMenuHotkey(TEXT("F1"), false));
    TestFalse(TEXT("Ctrl+F2 is not the menu hotkey"), IsMenuHotkey(TEXT("F2"), true));

    TestTrue(TEXT("Ctrl+F2 toggles the stats"), IsStatsHotkey(TEXT("F2"), true));
    TestTrue(TEXT("Ctrl+f2 toggles the stats"), IsStatsHotkey(TEXT("f2"), true));
    TestFalse(TEXT("F2 without Ctrl"), IsStatsHotkey(TEXT("F2"), false));
    TestFalse(TEXT("Ctrl+F1 is not the stats hotkey"), IsStatsHotkey(TEXT("F1"), true));

    // Other keys never match, with or without Ctrl
    const TCHAR* others[] = { TEXT(""), TEXT("Escape"), TEXT("F3"), TEXT("F11"), TEXT("F12"), TEXT("F"), TEXT("1"),
        TEXT("LeftControl"), TEXT("K"), TEXT(" F1"), TEXT("F1 ") };
    for (const TCHAR* key : others)
    {
        TestFalse(FString::Printf(TEXT("Ctrl+'%s' is not the menu hotkey"), key), IsMenuHotkey(key, true));
        TestFalse(FString::Printf(TEXT("'%s' is not the menu hotkey"), key), IsMenuHotkey(key, false));
        TestFalse(FString::Printf(TEXT("Ctrl+'%s' is not the stats hotkey"), key), IsStatsHotkey(key, true));
        TestFalse(FString::Printf(TEXT("'%s' is not the stats hotkey"), key), IsStatsHotkey(key, false));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyBindingsReservedFunctionKeysTest, "SOLTest.KeyBindings.ReservedFunctionKeys",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// F1 and F2 are reserved like Escape (refused by every bind path); F3 and F4 stay bindable
bool FSOLKeyBindingsReservedFunctionKeysTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    const TCHAR* reserved[] = { TEXT("F1"), TEXT("F2"), TEXT("f1"), TEXT("f2") };
    for (const TCHAR* key : reserved)
    {
        TestTrue(FString::Printf(TEXT("'%s' reserved"), key), IsReservedKey(key));
        TestFalse(FString::Printf(TEXT("'%s' refused"), key), CanBindKey(key));
        TestFalse(FString::Printf(TEXT("fire refuses '%s'"), key), CanBindKeyTo(KEYBINDINGS_FIRE, key));
        TestFalse(FString::Printf(TEXT("boost refuses '%s'"), key), CanBindKeyTo(KEYBINDINGS_BOOST, key));
    }
    const TCHAR* bindable[] = { TEXT("F3"), TEXT("F4") };
    for (const TCHAR* key : bindable)
    {
        TestFalse(FString::Printf(TEXT("'%s' not reserved"), key), IsReservedKey(key));
        TestTrue(FString::Printf(TEXT("'%s' accepted"), key), CanBindKey(key));
        TestTrue(FString::Printf(TEXT("fire may take '%s'"), key), CanBindKeyTo(KEYBINDINGS_FIRE, key));
    }

    // Rebind refuses F1 and F2 and leaves the keys alone, but takes F3
    TArray<FString> keys = DefaultKeys();
    const TArray<FString> before = keys;
    int32 conflict = 123;
    TestTrue(TEXT("rebind to F1 refused"), Rebind(keys, KEYBINDINGS_FIRE, TEXT("F1"), conflict)
        == ERebindResult::Refused);
    TestEqual(TEXT("refusal reports no conflict"), conflict, INDEX_NONE);
    TestTrue(TEXT("rebind to F2 refused"), Rebind(keys, KEYBINDINGS_FIRE, TEXT("F2"), conflict)
        == ERebindResult::Refused);
    TestTrue(TEXT("refusals left the keys alone"), keys == before);
    TestTrue(TEXT("rebind to F3 binds"), Rebind(keys, KEYBINDINGS_FIRE, TEXT("F3"), conflict) == ERebindResult::Bound);
    TestEqual(TEXT("fire is F3"), keys[KEYBINDINGS_FIRE], FString(TEXT("F3")));

    // A saved override onto F1 or F2 is ignored; one onto F4 applies
    TMap<FString, FString> overrides;
    overrides.Add(TEXT("Fire"), TEXT("F1"));
    overrides.Add(TEXT("Boost"), TEXT("F2"));
    overrides.Add(TEXT("DropTarget"), TEXT("F4"));
    const TArray<FString> resolved = ResolveKeys(overrides);
    TestEqual(TEXT("F1 override ignored"), resolved[KEYBINDINGS_FIRE], FString(TEXT("LeftMouseButton")));
    TestEqual(TEXT("F2 override ignored"), resolved[KEYBINDINGS_BOOST], FString(TEXT("Tab")));
    TestEqual(TEXT("F4 override applied"), resolved[KEYBINDINGS_DROP], FString(TEXT("F4")));
    return true;
}

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns how many Controls rows have exactly this bindings text; outRow is the last such row's index
    int32 KeyBindingsCountControlRows(const TCHAR* bindings, int32& outRow)
    {
        int32 count = 0;
        outRow = INDEX_NONE;
        for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::CONTROL_ROWS)); ++index)
        {
            if (FCString::Strcmp(SOLMenuText::CONTROL_ROWS[index].Bindings, bindings) == 0)
            {
                ++count;
                outRow = index;
            }
        }
        return count;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyBindingsHotkeyControlRowsTest, "SOLTest.KeyBindings.HotkeyControlRows",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The Controls page lists Ctrl+F1 (Menu) and Ctrl+F2 (Demo) once each, shown as "Ctrl+F1" and "Ctrl+F2"
bool FSOLKeyBindingsHotkeyControlRowsTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    int32 menuRow = INDEX_NONE;
    TestEqual(TEXT("one Ctrl+F1 row"), KeyBindingsCountControlRows(TEXT("LeftControl+F1"), menuRow), 1);
    if (menuRow != INDEX_NONE)
    {
        const SOLMenuText::FControlRow& row = SOLMenuText::CONTROL_ROWS[menuRow];
        TestEqual(TEXT("Ctrl+F1 row area"), FString(row.Area), FString(TEXT("Menu")));
        TestFalse(TEXT("Ctrl+F1 row has an action"), FString(row.Action).IsEmpty());
        TestEqual(TEXT("Ctrl+F1 shown"), SOLMenuContent::FormatBindings(row.Bindings, keys),
            FString(TEXT("Ctrl+F1")));
    }

    int32 demoRow = INDEX_NONE;
    TestEqual(TEXT("one Ctrl+F2 row"), KeyBindingsCountControlRows(TEXT("LeftControl+F2"), demoRow), 1);
    if (demoRow != INDEX_NONE)
    {
        const SOLMenuText::FControlRow& row = SOLMenuText::CONTROL_ROWS[demoRow];
        TestEqual(TEXT("Ctrl+F2 row area"), FString(row.Area), FString(TEXT("Demo")));
        TestFalse(TEXT("Ctrl+F2 row has an action"), FString(row.Action).IsEmpty());
        TestEqual(TEXT("Ctrl+F2 shown"), SOLMenuContent::FormatBindings(row.Bindings, keys),
            FString(TEXT("Ctrl+F2")));
    }

    // The chords are fixed keys, so the default overload shows the same text
    TestEqual(TEXT("Ctrl+F1 default overload"), SOLMenuContent::FormatBindings(TEXT("LeftControl+F1")),
        FString(TEXT("Ctrl+F1")));
    TestEqual(TEXT("Ctrl+F2 default overload"), SOLMenuContent::FormatBindings(TEXT("LeftControl+F2")),
        FString(TEXT("Ctrl+F2")));
    return true;
}

// Stop spin save-migration conflict (issue #18 review fix L5, SDD 18 Amendment 7), written against the contract in
// Docs/Plans/18-newtonian-flight-core-plan.md

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyBindingsStopSpinConflictTest, "SOLTest.KeyBindings.StopSpinOverrideConflict",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A save made before Stop spin existed that put Z on another action resolves with both on Z, and both are flagged
// as a conflict like any other duplicate until the player rebinds one
bool FSOLKeyBindingsStopSpinConflictTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    const int32 stopSpin = static_cast<int32>(EBinding::StopSpin);
    TestEqual(TEXT("Stop spin found by its id"), FindById(GetDef(stopSpin).Id), stopSpin);
    TestEqual(TEXT("Stop spin defaults to Z"), DefaultKeys()[stopSpin], FString(TEXT("Z")));

    // The old save: drop target on Z (Stop spin has no entry, so it keeps its default Z)
    TMap<FString, FString> overrides;
    overrides.Add(GetDef(KEYBINDINGS_DROP).Id, TEXT("Z"));
    TArray<FString> keys = ResolveKeys(overrides);
    TestEqual(TEXT("override applied"), keys[KEYBINDINGS_DROP], FString(TEXT("Z")));
    TestEqual(TEXT("Stop spin keeps Z"), keys[stopSpin], FString(TEXT("Z")));
    TestTrue(TEXT("Stop spin flagged"), HasConflict(keys, stopSpin));
    TestTrue(TEXT("drop target flagged"), HasConflict(keys, KEYBINDINGS_DROP));
    TestEqual(TEXT("FindConflict from Stop spin"), FindConflict(keys, stopSpin, keys[stopSpin]), KEYBINDINGS_DROP);
    TestEqual(TEXT("FindConflict from drop target, any case"), FindConflict(keys, KEYBINDINGS_DROP, TEXT("z")),
        stopSpin);
    for (int32 binding = 0; binding < BINDING_COUNT; ++binding)
    {
        if (binding != stopSpin && binding != KEYBINDINGS_DROP)
        {
            TestFalse(FString::Printf(TEXT("%s not flagged"), GetDef(binding).Id), HasConflict(keys, binding));
        }
    }

    // A lower-case key in the save is the same duplicate
    TMap<FString, FString> lowerCase;
    lowerCase.Add(GetDef(KEYBINDINGS_FIRE).Id, TEXT("z"));
    const TArray<FString> lowerKeys = ResolveKeys(lowerCase);
    TestTrue(TEXT("lower-case z: Stop spin flagged"), HasConflict(lowerKeys, stopSpin));
    TestTrue(TEXT("lower-case z: fire flagged"), HasConflict(lowerKeys, KEYBINDINGS_FIRE));

    // Rebinding either one clears both flags
    int32 conflict = 123;
    TestTrue(TEXT("drop target back to G"), Rebind(keys, KEYBINDINGS_DROP, TEXT("G"), conflict)
        == ERebindResult::Bound);
    TestEqual(TEXT("no conflict on G"), conflict, INDEX_NONE);
    TestFalse(TEXT("Stop spin clear"), HasConflict(keys, stopSpin));
    TestFalse(TEXT("drop target clear"), HasConflict(keys, KEYBINDINGS_DROP));
    return true;
}

#endif
