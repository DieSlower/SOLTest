/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "SOLConstants.h"
#include "UI/SOLOverlayModel.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #22 overlay model (SDD 22 section 2.3), written against the contract in
// Docs/Plans/22-overlay-icons-and-interaction-menus-plan.md before the production code existed.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    using SOLOverlay::EOverlay;

    // One expected table row, restated so the tests stay independent of the production table
    struct FOverlayModelExpectedRow
    {
        EOverlay Overlay;
        const TCHAR* Id;
        const TCHAR* Label;
        bool bDefaultOn;
        int32 Parent;
    };

    constexpr int32 OVERLAYMODEL_ICONS = static_cast<int32>(EOverlay::Icons);

    const FOverlayModelExpectedRow OVERLAYMODEL_ROWS[] =
    {
        { EOverlay::Icons, TEXT("Icons"), TEXT("Icons"), true, INDEX_NONE },
        { EOverlay::IconStars, TEXT("IconStars"), TEXT("Stars"), true, OVERLAYMODEL_ICONS },
        { EOverlay::IconPlanets, TEXT("IconPlanets"), TEXT("Planets"), true, OVERLAYMODEL_ICONS },
        { EOverlay::IconMoons, TEXT("IconMoons"), TEXT("Moons"), true, OVERLAYMODEL_ICONS },
        { EOverlay::IconDwarfs, TEXT("IconDwarfs"), TEXT("Dwarf planets"), true, OVERLAYMODEL_ICONS },
        { EOverlay::IconAsteroids, TEXT("IconAsteroids"), TEXT("Asteroids"), true, OVERLAYMODEL_ICONS },
        { EOverlay::IconRings, TEXT("IconRings"), TEXT("Rings"), true, OVERLAYMODEL_ICONS },
        { EOverlay::IconShips, TEXT("IconShips"), TEXT("Ships"), true, OVERLAYMODEL_ICONS },
        { EOverlay::IconTargets, TEXT("IconTargets"), TEXT("Targets"), true, OVERLAYMODEL_ICONS },
        { EOverlay::PlanetOrbits, TEXT("PlanetOrbits"), TEXT("Planet Orbits"), true, INDEX_NONE },
        { EOverlay::AsteroidOrbits, TEXT("AsteroidOrbits"), TEXT("Asteroid Orbits"), false, INDEX_NONE },
        { EOverlay::MoonOrbits, TEXT("MoonOrbits"), TEXT("Moon Orbits"), true, INDEX_NONE },
        { EOverlay::OtherOrbits, TEXT("OtherOrbits"), TEXT("Other Orbits"), true, INDEX_NONE },
        { EOverlay::ShipsOnRadar, TEXT("ShipsOnRadar"), TEXT("Ships on Radar"), true, INDEX_NONE },
        { EOverlay::Darkening, TEXT("Darkening"), TEXT("Darkening"), true, INDEX_NONE },
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns the mask bit of one overlay row
    uint32 OverlayModelBit(const EOverlay overlay)
    {
        return 1u << static_cast<uint32>(overlay);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the icon category rows (the children of Icons)
    TArray<EOverlay> OverlayModelIconCategories()
    {
        return { EOverlay::IconStars, EOverlay::IconPlanets, EOverlay::IconMoons, EOverlay::IconDwarfs,
            EOverlay::IconAsteroids, EOverlay::IconRings, EOverlay::IconShips, EOverlay::IconTargets };
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelTableTest, "SOLTest.OverlayModel.Table",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The table has one row per overlay in enum order with the contract's ids, labels, defaults and parents
bool FSOLOverlayModelTableTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;
    TestEqual(TEXT("row count"), OVERLAY_COUNT, static_cast<int32>(UE_ARRAY_COUNT(OVERLAYMODEL_ROWS)));
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(OVERLAYMODEL_ROWS)); ++index)
    {
        const FOverlayModelExpectedRow& expected = OVERLAYMODEL_ROWS[index];
        TestEqual(FString::Printf(TEXT("%s enum order"), expected.Id), static_cast<int32>(expected.Overlay), index);
        const FOverlayDef& def = GetDef(index);
        TestEqual(FString::Printf(TEXT("%s id"), expected.Id), FString(def.Id), FString(expected.Id));
        TestEqual(FString::Printf(TEXT("%s label"), expected.Id), FString(def.Label), FString(expected.Label));
        TestEqual(FString::Printf(TEXT("%s default"), expected.Id), def.bDefaultOn, expected.bDefaultOn);
        TestEqual(FString::Printf(TEXT("%s parent"), expected.Id), def.Parent, expected.Parent);
        TestEqual(FString::Printf(TEXT("%s found by id"), expected.Id), FindById(expected.Id), index);
        TestEqual(FString::Printf(TEXT("%s found lower-case"), expected.Id), FindById(FString(expected.Id).ToLower()),
            index);
        TestEqual(FString::Printf(TEXT("%s found upper-case"), expected.Id), FindById(FString(expected.Id).ToUpper()),
            index);
    }
    TestEqual(TEXT("unknown id"), FindById(TEXT("NoSuchOverlay")), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("empty id"), FindById(FString()), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("a label is not an id"), FindById(TEXT("Planet Orbits")), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("GetDef clamps a negative index"), FString(GetDef(-4).Id), FString(GetDef(0).Id));
    TestEqual(TEXT("GetDef clamps a large index"), FString(GetDef(OVERLAY_COUNT + 9).Id),
        FString(GetDef(OVERLAY_COUNT - 1).Id));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelMasksTest, "SOLTest.OverlayModel.Masks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AllBitsMask has one bit per row, DefaultMask is every row but Asteroid Orbits, and the default prefs hold one
// profile with every slot at the default mask
bool FSOLOverlayModelMasksTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;
    const uint32 allBits = (1u << OVERLAY_COUNT) - 1u;
    TestEqual(TEXT("all bits"), AllBitsMask(), allBits);
    TestEqual(TEXT("default mask"), DefaultMask(), allBits & ~OverlayModelBit(EOverlay::AsteroidOrbits));
    TestEqual(TEXT("default mask within the valid bits"), DefaultMask() & ~AllBitsMask(), 0u);

    const FOverlayPrefs prefs = MakeDefaultPrefs();
    TestEqual(TEXT("one profile"), prefs.ProfileCount, 1);
    TestEqual(TEXT("profile 0 active"), prefs.ActiveProfile, 0);
    for (int32 slot = 0; slot < SOL::Overlay::MAX_PROFILES; ++slot)
    {
        TestEqual(FString::Printf(TEXT("slot %d default"), slot), prefs.Masks[slot], DefaultMask());
    }
    for (const FOverlayModelExpectedRow& row : OVERLAYMODEL_ROWS)
    {
        TestEqual(FString::Printf(TEXT("%s enabled by default"), row.Id), IsEnabled(prefs, row.Overlay),
            row.bDefaultOn);
    }
    TestEqual(TEXT("five profiles of room"), SOL::Overlay::MAX_PROFILES, 5);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelEnableToggleTest, "SOLTest.OverlayModel.EnableToggle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SetEnabled and Toggle act on the active profile only; an out-of-range row reads false and is never written
bool FSOLOverlayModelEnableToggleTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;
    FOverlayPrefs prefs = MakeDefaultPrefs();
    prefs.ProfileCount = 3;
    prefs.ActiveProfile = 1;
    prefs.Masks[0] = DefaultMask();
    prefs.Masks[1] = DefaultMask();
    prefs.Masks[2] = 0u;

    SetEnabled(prefs, EOverlay::IconMoons, false);
    TestFalse(TEXT("moons unchecked"), IsEnabled(prefs, EOverlay::IconMoons));
    TestEqual(TEXT("active mask lost the moon bit"), prefs.Masks[1],
        DefaultMask() & ~OverlayModelBit(EOverlay::IconMoons));
    TestEqual(TEXT("profile 0 untouched"), prefs.Masks[0], DefaultMask());
    TestEqual(TEXT("profile 2 untouched"), prefs.Masks[2], 0u);

    SetEnabled(prefs, EOverlay::AsteroidOrbits, true);
    TestTrue(TEXT("asteroid orbits checked"), IsEnabled(prefs, EOverlay::AsteroidOrbits));
    SetEnabled(prefs, EOverlay::AsteroidOrbits, true);
    TestTrue(TEXT("checking twice keeps it checked"), IsEnabled(prefs, EOverlay::AsteroidOrbits));

    // Toggle returns the new state
    TestTrue(TEXT("toggle moons on"), Toggle(prefs, EOverlay::IconMoons));
    TestTrue(TEXT("moons on"), IsEnabled(prefs, EOverlay::IconMoons));
    TestFalse(TEXT("toggle darkening off"), Toggle(prefs, EOverlay::Darkening));
    TestFalse(TEXT("darkening off"), IsEnabled(prefs, EOverlay::Darkening));
    TestTrue(TEXT("toggle darkening back on"), Toggle(prefs, EOverlay::Darkening));
    TestEqual(TEXT("profile 0 still untouched"), prefs.Masks[0], DefaultMask());
    TestEqual(TEXT("profile 2 still untouched"), prefs.Masks[2], 0u);

    // Switching the active profile reads the other mask
    prefs.ActiveProfile = 2;
    TestFalse(TEXT("profile 2 has nothing checked"), IsEnabled(prefs, EOverlay::Icons));
    prefs.ActiveProfile = 0;
    TestFalse(TEXT("profile 0 keeps asteroid orbits off"), IsEnabled(prefs, EOverlay::AsteroidOrbits));

    // Out-of-range rows
    const FOverlayPrefs before = prefs;
    TestFalse(TEXT("Count reads false"), IsEnabled(prefs, EOverlay::Count));
    TestFalse(TEXT("a large value reads false"), IsEnabled(prefs, static_cast<EOverlay>(200)));
    SetEnabled(prefs, EOverlay::Count, true);
    SetEnabled(prefs, static_cast<EOverlay>(31), true);
    for (int32 slot = 0; slot < SOL::Overlay::MAX_PROFILES; ++slot)
    {
        TestEqual(FString::Printf(TEXT("slot %d unchanged by an out-of-range set"), slot), prefs.Masks[slot],
            before.Masks[slot]);
    }
    TestFalse(TEXT("Count still reads false"), IsEnabled(prefs, EOverlay::Count));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelShownTest, "SOLTest.OverlayModel.IsShown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The master hides everything and restores exactly the same set; unchecking Icons hides every icon category while
// they stay checked
bool FSOLOverlayModelShownTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;
    FOverlayPrefs prefs = MakeDefaultPrefs();
    SetEnabled(prefs, EOverlay::IconShips, false);
    SetEnabled(prefs, EOverlay::MoonOrbits, false);

    // Shown with the master on: checked and parent checked
    TArray<bool> shownBefore;
    for (int32 index = 0; index < OVERLAY_COUNT; ++index)
    {
        const EOverlay overlay = static_cast<EOverlay>(index);
        const bool bExpected = IsEnabled(prefs, overlay);
        shownBefore.Add(IsShown(prefs, true, overlay));
        TestEqual(FString::Printf(TEXT("%s shown = checked"), GetDef(index).Id), shownBefore.Last(), bExpected);
    }
    TestFalse(TEXT("ships hidden"), IsShown(prefs, true, EOverlay::IconShips));
    TestFalse(TEXT("asteroid orbits hidden by default"), IsShown(prefs, true, EOverlay::AsteroidOrbits));

    // Master off hides everything, without changing any checkbox
    const uint32 maskBefore = prefs.Masks[0];
    for (int32 index = 0; index < OVERLAY_COUNT; ++index)
    {
        TestFalse(FString::Printf(TEXT("%s hidden by the master"), GetDef(index).Id),
            IsShown(prefs, false, static_cast<EOverlay>(index)));
    }
    TestEqual(TEXT("master leaves the mask alone"), prefs.Masks[0], maskBefore);

    // Master back on shows exactly the same set
    for (int32 index = 0; index < OVERLAY_COUNT; ++index)
    {
        TestEqual(FString::Printf(TEXT("%s remembered"), GetDef(index).Id),
            IsShown(prefs, true, static_cast<EOverlay>(index)), shownBefore[index]);
    }

    // Unchecking Icons hides every category but keeps their checkboxes
    SetEnabled(prefs, EOverlay::Icons, false);
    for (const EOverlay category : OverlayModelIconCategories())
    {
        TestFalse(FString::Printf(TEXT("%s hidden with Icons off"), GetDef(static_cast<int32>(category)).Id),
            IsShown(prefs, true, category));
    }
    TestTrue(TEXT("stars still checked"), IsEnabled(prefs, EOverlay::IconStars));
    TestTrue(TEXT("planet orbits (top-level) still shown"), IsShown(prefs, true, EOverlay::PlanetOrbits));
    TestTrue(TEXT("radar ships (top-level) still shown"), IsShown(prefs, true, EOverlay::ShipsOnRadar));
    SetEnabled(prefs, EOverlay::Icons, true);
    TestTrue(TEXT("stars back with Icons"), IsShown(prefs, true, EOverlay::IconStars));
    TestFalse(TEXT("ships stay unchecked"), IsShown(prefs, true, EOverlay::IconShips));
    TestFalse(TEXT("Count never shown"), IsShown(prefs, true, EOverlay::Count));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelOrbitShownTest, "SOLTest.OverlayModel.OrbitShown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Only the four orbit rows are orbit overlays, and their lines need the master, their checkbox and the orbit switch
bool FSOLOverlayModelOrbitShownTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;
    const EOverlay orbitRows[] = { EOverlay::PlanetOrbits, EOverlay::AsteroidOrbits, EOverlay::MoonOrbits,
        EOverlay::OtherOrbits };
    for (int32 index = 0; index < OVERLAY_COUNT; ++index)
    {
        const EOverlay overlay = static_cast<EOverlay>(index);
        bool bOrbit = false;
        for (const EOverlay orbit : orbitRows)
        {
            bOrbit |= orbit == overlay;
        }
        TestEqual(FString::Printf(TEXT("%s orbit overlay"), GetDef(index).Id), IsOrbitOverlay(overlay), bOrbit);
    }
    TestFalse(TEXT("Count is no orbit overlay"), IsOrbitOverlay(EOverlay::Count));

    const FOverlayPrefs prefs = MakeDefaultPrefs();
    TestTrue(TEXT("planet orbits shown"), IsOrbitShown(prefs, true, true, EOverlay::PlanetOrbits));
    TestTrue(TEXT("moon orbits shown"), IsOrbitShown(prefs, true, true, EOverlay::MoonOrbits));
    TestTrue(TEXT("other orbits shown"), IsOrbitShown(prefs, true, true, EOverlay::OtherOrbits));
    TestFalse(TEXT("asteroid orbits unchecked by default"), IsOrbitShown(prefs, true, true, EOverlay::AsteroidOrbits));
    TestFalse(TEXT("orbit switch off hides"), IsOrbitShown(prefs, true, false, EOverlay::PlanetOrbits));
    TestFalse(TEXT("master off hides"), IsOrbitShown(prefs, false, true, EOverlay::PlanetOrbits));
    TestFalse(TEXT("a non-orbit row is never an orbit line"), IsOrbitShown(prefs, true, true, EOverlay::Icons));
    TestFalse(TEXT("darkening is no orbit line"), IsOrbitShown(prefs, true, true, EOverlay::Darkening));

    FOverlayPrefs withAsteroids = prefs;
    SetEnabled(withAsteroids, EOverlay::AsteroidOrbits, true);
    TestTrue(TEXT("checked asteroid orbits shown"), IsOrbitShown(withAsteroids, true, true, EOverlay::AsteroidOrbits));
    SetEnabled(withAsteroids, EOverlay::PlanetOrbits, false);
    TestFalse(TEXT("unchecked planet orbits hidden"), IsOrbitShown(withAsteroids, true, true, EOverlay::PlanetOrbits));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelSanitizedTest, "SOLTest.OverlayModel.Sanitized",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Sanitized clamps the profile count and active profile, drops invalid bits and resets unused slots to the default
bool FSOLOverlayModelSanitizedTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;
    const int32 maxProfiles = SOL::Overlay::MAX_PROFILES;

    // Too few profiles and a negative active index
    FOverlayPrefs low;
    for (int32 slot = 0; slot < maxProfiles; ++slot)
    {
        low.Masks[slot] = 0xFFFFFFFFu;
    }
    low.ProfileCount = 0;
    low.ActiveProfile = -3;
    const FOverlayPrefs lowOut = Sanitized(low);
    TestEqual(TEXT("count clamped up to 1"), lowOut.ProfileCount, 1);
    TestEqual(TEXT("active clamped up to 0"), lowOut.ActiveProfile, 0);
    TestEqual(TEXT("invalid bits dropped"), lowOut.Masks[0], AllBitsMask());
    for (int32 slot = 1; slot < maxProfiles; ++slot)
    {
        TestEqual(FString::Printf(TEXT("unused slot %d reset"), slot), lowOut.Masks[slot], DefaultMask());
    }

    // Too many profiles and an active index past the end
    FOverlayPrefs high;
    for (int32 slot = 0; slot < maxProfiles; ++slot)
    {
        high.Masks[slot] = static_cast<uint32>(slot) | (1u << 31);
    }
    high.ProfileCount = maxProfiles + 4;
    high.ActiveProfile = 99;
    const FOverlayPrefs highOut = Sanitized(high);
    TestEqual(TEXT("count clamped down to MAX_PROFILES"), highOut.ProfileCount, maxProfiles);
    TestEqual(TEXT("active clamped to the last profile"), highOut.ActiveProfile, maxProfiles - 1);
    for (int32 slot = 0; slot < maxProfiles; ++slot)
    {
        TestEqual(FString::Printf(TEXT("slot %d keeps its valid bits"), slot), highOut.Masks[slot],
            static_cast<uint32>(slot) & AllBitsMask());
    }

    // The active index clamps to the profile count, not to MAX_PROFILES
    FOverlayPrefs three = MakeDefaultPrefs();
    three.ProfileCount = 3;
    three.ActiveProfile = 4;
    three.Masks[1] = 0x5u;
    three.Masks[3] = 0x7u;
    three.Masks[4] = 0u;
    const FOverlayPrefs threeOut = Sanitized(three);
    TestEqual(TEXT("three profiles kept"), threeOut.ProfileCount, 3);
    TestEqual(TEXT("active clamped to 2"), threeOut.ActiveProfile, 2);
    TestEqual(TEXT("used slot 1 kept"), threeOut.Masks[1], 0x5u);
    TestEqual(TEXT("slot 3 reset"), threeOut.Masks[3], DefaultMask());
    TestEqual(TEXT("slot 4 reset"), threeOut.Masks[4], DefaultMask());

    // Valid prefs pass through, and sanitizing is idempotent
    const FOverlayPrefs defaults = MakeDefaultPrefs();
    const FOverlayPrefs defaultsOut = Sanitized(defaults);
    TestEqual(TEXT("defaults: count"), defaultsOut.ProfileCount, 1);
    TestEqual(TEXT("defaults: active"), defaultsOut.ActiveProfile, 0);
    TestEqual(TEXT("defaults: mask"), defaultsOut.Masks[0], DefaultMask());
    const FOverlayPrefs twice = Sanitized(threeOut);
    TestEqual(TEXT("idempotent count"), twice.ProfileCount, threeOut.ProfileCount);
    TestEqual(TEXT("idempotent active"), twice.ActiveProfile, threeOut.ActiveProfile);
    for (int32 slot = 0; slot < maxProfiles; ++slot)
    {
        TestEqual(FString::Printf(TEXT("idempotent slot %d"), slot), twice.Masks[slot], threeOut.Masks[slot]);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelAreEqualTest, "SOLTest.OverlayModel.AreEqual",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AreEqual compares sanitized prefs: unused slots and invalid bits are ignored, used masks, the count and the
// active profile are not
bool FSOLOverlayModelAreEqualTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;
    const FOverlayPrefs base = MakeDefaultPrefs();
    TestTrue(TEXT("equal to itself"), AreEqual(base, base));

    FOverlayPrefs unusedSlot = base;
    unusedSlot.Masks[3] = 0x1u;
    TestTrue(TEXT("an unused slot is ignored"), AreEqual(base, unusedSlot));

    FOverlayPrefs invalidBit = base;
    invalidBit.Masks[0] |= 1u << 31;
    TestTrue(TEXT("an invalid bit is ignored"), AreEqual(base, invalidBit));

    FOverlayPrefs oneBit = base;
    Toggle(oneBit, EOverlay::IconRings);
    TestFalse(TEXT("one overlay bit differs"), AreEqual(base, oneBit));
    TestFalse(TEXT("symmetric"), AreEqual(oneBit, base));

    FOverlayPrefs twoProfiles = base;
    twoProfiles.ProfileCount = 2;
    TestFalse(TEXT("profile count differs"), AreEqual(base, twoProfiles));

    FOverlayPrefs secondActive = twoProfiles;
    secondActive.ActiveProfile = 1;
    TestFalse(TEXT("active profile differs"), AreEqual(twoProfiles, secondActive));

    FOverlayPrefs secondMask = twoProfiles;
    secondMask.Masks[1] = 0u;
    TestFalse(TEXT("a used second slot differs"), AreEqual(twoProfiles, secondMask));

    // Out-of-range values that sanitize to the same prefs compare equal
    FOverlayPrefs overA = base;
    overA.ProfileCount = -2;
    overA.ActiveProfile = 7;
    TestTrue(TEXT("clamped prefs equal the defaults"), AreEqual(overA, base));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelSavedProfileTest, "SOLTest.OverlayModel.SavedProfile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A profile saves as every id -> checked and loads back case-insensitively; unknown ids are ignored and missing ids
// take their default
bool FSOLOverlayModelSavedProfileTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;

    // ToSavedProfile names every row once with its bit
    const uint32 mask = OverlayModelBit(EOverlay::Icons) | OverlayModelBit(EOverlay::IconMoons)
        | OverlayModelBit(EOverlay::AsteroidOrbits) | OverlayModelBit(EOverlay::Darkening);
    const TMap<FString, bool> saved = ToSavedProfile(mask);
    TestEqual(TEXT("one entry per row"), saved.Num(), OVERLAY_COUNT);
    for (const FOverlayModelExpectedRow& row : OVERLAYMODEL_ROWS)
    {
        const bool* value = saved.Find(row.Id);
        if (TestNotNull(FString::Printf(TEXT("%s saved"), row.Id), value))
        {
            TestEqual(FString::Printf(TEXT("%s saved state"), row.Id), *value,
                (mask & OverlayModelBit(row.Overlay)) != 0u);
        }
    }

    // Round trips, invalid bits dropped
    const uint32 masks[] = { 0u, DefaultMask(), AllBitsMask(), mask, 0xFFFFFFFFu, 0x2AAAu };
    for (const uint32 value : masks)
    {
        TestEqual(FString::Printf(TEXT("round trip 0x%08x"), value), FromSavedProfile(ToSavedProfile(value)),
            value & AllBitsMask());
    }

    // Empty data is the default; missing rows take their default
    TestEqual(TEXT("empty -> default"), FromSavedProfile(TMap<FString, bool>()), DefaultMask());
    TMap<FString, bool> partial;
    partial.Add(TEXT("IconPlanets"), false);
    partial.Add(TEXT("AsteroidOrbits"), true);
    TestEqual(TEXT("partial: two rows changed"), FromSavedProfile(partial),
        (DefaultMask() & ~OverlayModelBit(EOverlay::IconPlanets)) | OverlayModelBit(EOverlay::AsteroidOrbits));

    // Ids are case-insensitive, unknown ids are ignored
    TMap<FString, bool> mixed;
    mixed.Add(TEXT("darkening"), false);
    mixed.Add(TEXT("ICONSTARS"), false);
    mixed.Add(TEXT("NoSuchOverlay"), false);
    mixed.Add(TEXT(""), false);
    TestEqual(TEXT("case-insensitive ids, unknown ignored"), FromSavedProfile(mixed),
        DefaultMask() & ~OverlayModelBit(EOverlay::Darkening) & ~OverlayModelBit(EOverlay::IconStars));
    TMap<FString, bool> unknownOnly;
    unknownOnly.Add(TEXT("Removed"), true);
    TestEqual(TEXT("only unknown ids -> default"), FromSavedProfile(unknownOnly), DefaultMask());
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
