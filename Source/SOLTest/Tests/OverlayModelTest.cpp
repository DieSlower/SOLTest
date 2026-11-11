/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "SOLConstants.h"
#include "UI/SOLOverlayModel.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #22 overlay model (SDD 22 section 2.3), written against the contract in
// Docs/Plans/22-overlay-icons-and-interaction-menus-plan.md before the production code existed. Rewritten for the
// issue #34 overlay profiles (SDD 34 sections 2, 4 and 11) from that contract: Space Dust is no longer a row, and each
// profile carries its own enabled / used state, master O state and Alt+O orbit-lines switch. Extended for the issue #38
// Lanes row (SDD 38 section 6 case 17) from that contract: 16 rows, Lanes at index 14, Darkening last at index 15.

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

    // The row count after issue #34 removed Space Dust and issue #38 added Lanes (SDD 38 D17)
    constexpr int32 OVERLAYMODEL_ROW_COUNT = 16;

    // The Lanes row's and the Darkening row's indices (SDD 38 D17: Lanes between Ships on Radar and Darkening)
    constexpr int32 OVERLAYMODEL_LANES_INDEX = 14;
    constexpr int32 OVERLAYMODEL_DARKENING_INDEX = 15;

    // The profile mask with every one of the five profiles set
    constexpr int32 OVERLAYMODEL_ALL_PROFILES = 0x1F;

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
        { EOverlay::Lanes, TEXT("Lanes"), TEXT("Lanes"), true, INDEX_NONE },
        { EOverlay::Darkening, TEXT("Darkening"), TEXT("Darkening"), true, INDEX_NONE },
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns the mask bit of one overlay row
    uint32 OverlayModelBit(const EOverlay overlay)
    {
        return 1u << static_cast<uint32>(overlay);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a per-profile bit set (enabled, used, master or orbit lines) has a profile's bit
    bool OverlayModelHasBit(const uint32 bits, const int32 profile)
    {
        return ((bits >> profile) & 1u) != 0u;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the icon category rows (the children of Icons)
    TArray<EOverlay> OverlayModelIconCategories()
    {
        return { EOverlay::IconStars, EOverlay::IconPlanets, EOverlay::IconMoons, EOverlay::IconDwarfs,
            EOverlay::IconAsteroids, EOverlay::IconRings, EOverlay::IconShips, EOverlay::IconTargets };
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks that two prefs match field by field (raw, not sanitized)
    void OverlayModelCheckSamePrefs(FAutomationTestBase& test, const SOLOverlay::FOverlayPrefs& actual,
        const SOLOverlay::FOverlayPrefs& expected, const FString& what)
    {
        for (int32 slot = 0; slot < SOL::Overlay::MAX_PROFILES; ++slot)
        {
            test.TestEqual(FString::Printf(TEXT("%s: mask %d"), *what, slot), actual.Masks[slot],
                expected.Masks[slot]);
        }
        test.TestEqual(what + TEXT(": enabled"), static_cast<int32>(actual.EnabledProfiles),
            static_cast<int32>(expected.EnabledProfiles));
        test.TestEqual(what + TEXT(": used"), static_cast<int32>(actual.UsedProfiles),
            static_cast<int32>(expected.UsedProfiles));
        test.TestEqual(what + TEXT(": master"), static_cast<int32>(actual.MasterOnProfiles),
            static_cast<int32>(expected.MasterOnProfiles));
        test.TestEqual(what + TEXT(": orbit lines"), static_cast<int32>(actual.OrbitLinesProfiles),
            static_cast<int32>(expected.OrbitLinesProfiles));
        test.TestEqual(what + TEXT(": active"), actual.ActiveProfile, expected.ActiveProfile);
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
    TestEqual(TEXT("sixteen rows"), OVERLAY_COUNT, OVERLAYMODEL_ROW_COUNT);
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelNoSpaceDustTest, "SOLTest.OverlayModel.NoSpaceDust",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Issue #34 (SDD 34 D5, section 4.1): Space Dust is no longer an overlay row, so Darkening is the last row again;
// issue #38 (SDD 38 D17) puts Lanes before it, so Darkening moves to index 15
bool FSOLOverlayModelNoSpaceDustTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;
    TestEqual(TEXT("Darkening is the last row"), static_cast<int32>(EOverlay::Darkening), OVERLAY_COUNT - 1);
    TestEqual(TEXT("Darkening at index 15"), static_cast<int32>(EOverlay::Darkening), OVERLAYMODEL_DARKENING_INDEX);
    TestEqual(TEXT("Darkening id"), FString(GetDef(OVERLAY_COUNT - 1).Id), FString(TEXT("Darkening")));
    TestEqual(TEXT("SpaceDust is not an id"), FindById(TEXT("SpaceDust")), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("spacedust is not an id"), FindById(TEXT("spacedust")), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("Space Dust is not an id"), FindById(TEXT("Space Dust")), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("sixteen valid bits"), AllBitsMask(), 0xFFFFu);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelMasksTest, "SOLTest.OverlayModel.Masks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AllBitsMask has one bit per row, DefaultMask is every row but Asteroid Orbits, and the default prefs check the rows
// whose default is on
bool FSOLOverlayModelMasksTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;
    const uint32 allBits = (1u << OVERLAY_COUNT) - 1u;
    TestEqual(TEXT("all bits"), AllBitsMask(), allBits);
    TestEqual(TEXT("default mask"), DefaultMask(), allBits & ~OverlayModelBit(EOverlay::AsteroidOrbits));
    TestEqual(TEXT("default mask within the valid bits"), DefaultMask() & ~AllBitsMask(), 0u);

    const FOverlayPrefs prefs = MakeDefaultPrefs();
    for (const FOverlayModelExpectedRow& row : OVERLAYMODEL_ROWS)
    {
        TestEqual(FString::Printf(TEXT("%s enabled by default"), row.Id), IsEnabled(prefs, row.Overlay),
            row.bDefaultOn);
    }
    TestEqual(TEXT("five profiles of room"), SOL::Overlay::MAX_PROFILES, 5);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelDefaultPrefsTest, "SOLTest.OverlayModel.DefaultPrefs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// MakeDefaultPrefs: every mask the default, only profile 0 enabled and used, every master and orbit switch on,
// profile 0 active (SDD 34 section 4.3)
bool FSOLOverlayModelDefaultPrefsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;
    const FOverlayPrefs prefs = MakeDefaultPrefs();
    for (int32 slot = 0; slot < SOL::Overlay::MAX_PROFILES; ++slot)
    {
        TestEqual(FString::Printf(TEXT("slot %d default mask"), slot), prefs.Masks[slot], DefaultMask());
    }
    TestEqual(TEXT("only profile 0 enabled"), static_cast<int32>(prefs.EnabledProfiles), 0x01);
    TestEqual(TEXT("only profile 0 used"), static_cast<int32>(prefs.UsedProfiles), 0x01);
    TestEqual(TEXT("every master on"), static_cast<int32>(prefs.MasterOnProfiles), OVERLAYMODEL_ALL_PROFILES);
    TestEqual(TEXT("every orbit switch on"), static_cast<int32>(prefs.OrbitLinesProfiles), OVERLAYMODEL_ALL_PROFILES);
    TestEqual(TEXT("profile 0 active"), prefs.ActiveProfile, 0);
    for (int32 profile = 0; profile < SOL::Overlay::MAX_PROFILES; ++profile)
    {
        TestEqual(FString::Printf(TEXT("profile %d enabled"), profile), IsProfileEnabled(prefs, profile),
            profile == 0);
        TestEqual(FString::Printf(TEXT("profile %d used"), profile), IsProfileUsed(prefs, profile), profile == 0);
    }
    TestTrue(TEXT("master on"), IsMasterOn(prefs));
    TestTrue(TEXT("orbit lines on"), AreOrbitLinesOn(prefs));

    // The struct's own initializers (SDD 34 section 4.2)
    const FOverlayPrefs raw;
    TestEqual(TEXT("raw: enabled"), static_cast<int32>(raw.EnabledProfiles), 0x01);
    TestEqual(TEXT("raw: used"), static_cast<int32>(raw.UsedProfiles), 0x01);
    TestEqual(TEXT("raw: master"), static_cast<int32>(raw.MasterOnProfiles), OVERLAYMODEL_ALL_PROFILES);
    TestEqual(TEXT("raw: orbit lines"), static_cast<int32>(raw.OrbitLinesProfiles), OVERLAYMODEL_ALL_PROFILES);
    TestEqual(TEXT("raw: active"), raw.ActiveProfile, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelProfileConstantsTest, "SOLTest.OverlayModel.ProfileConstants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The issue #34 constants: a five-bit profile mask and the HUD tag timing (2 s shown, 0.5 s fade, D13)
bool FSOLOverlayModelProfileConstantsTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("all profiles mask"), static_cast<uint32>(SOL::Overlay::ALL_PROFILES_MASK), 0x1Fu);
    TestEqual(TEXT("all profiles mask = (1 << MAX_PROFILES) - 1"),
        static_cast<uint32>(SOL::Overlay::ALL_PROFILES_MASK), (1u << SOL::Overlay::MAX_PROFILES) - 1u);
    TestEqual(TEXT("tag shown for 2 s"), static_cast<double>(SOL::Overlay::PROFILE_TAG_SHOW_S), 2.0, 1.0e-9);
    TestEqual(TEXT("tag fades over 0.5 s"), static_cast<double>(SOL::Overlay::PROFILE_TAG_FADE_S), 0.5, 1.0e-9);
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
    prefs.EnabledProfiles = 0x07;
    prefs.UsedProfiles = 0x07;
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelIsProfileEnabledTest, "SOLTest.OverlayModel.IsProfileEnabled",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Profile 0 is always enabled, profiles 1-4 follow their bit, and an out-of-range profile is never enabled
bool FSOLOverlayModelIsProfileEnabledTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;
    FOverlayPrefs prefs = MakeDefaultPrefs();

    // A corrupt mask without bit 0 still has profile 0 enabled
    prefs.EnabledProfiles = 0;
    TestTrue(TEXT("corrupt: profile 0 enabled"), IsProfileEnabled(prefs, 0));
    for (int32 profile = 1; profile < SOL::Overlay::MAX_PROFILES; ++profile)
    {
        TestFalse(FString::Printf(TEXT("corrupt: profile %d disabled"), profile), IsProfileEnabled(prefs, profile));
    }

    // Profiles 2 and 4 on, 1 and 3 off
    prefs.EnabledProfiles = 0x15;
    TestTrue(TEXT("profile 0"), IsProfileEnabled(prefs, 0));
    TestFalse(TEXT("profile 1"), IsProfileEnabled(prefs, 1));
    TestTrue(TEXT("profile 2"), IsProfileEnabled(prefs, 2));
    TestFalse(TEXT("profile 3"), IsProfileEnabled(prefs, 3));
    TestTrue(TEXT("profile 4"), IsProfileEnabled(prefs, 4));

    // Out of range, even with every bit set
    prefs.EnabledProfiles = 0xFF;
    TestFalse(TEXT("-1"), IsProfileEnabled(prefs, -1));
    TestFalse(TEXT("5"), IsProfileEnabled(prefs, 5));
    TestFalse(TEXT("99"), IsProfileEnabled(prefs, 99));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelIsProfileUsedTest, "SOLTest.OverlayModel.IsProfileUsed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Profile 0 and every enabled profile are used; a disabled profile 1-4 follows its used bit; out of range is unused
bool FSOLOverlayModelIsProfileUsedTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;

    // Fresh prefs: only profile 0
    FOverlayPrefs prefs = MakeDefaultPrefs();
    for (int32 profile = 0; profile < SOL::Overlay::MAX_PROFILES; ++profile)
    {
        TestEqual(FString::Printf(TEXT("fresh: profile %d used"), profile), IsProfileUsed(prefs, profile),
            profile == 0);
    }

    // After an enable and a disable, the profile stays used
    SetProfileEnabled(prefs, 2, true);
    SetProfileEnabled(prefs, 2, false);
    TestFalse(TEXT("profile 2 disabled"), IsProfileEnabled(prefs, 2));
    TestTrue(TEXT("profile 2 used"), IsProfileUsed(prefs, 2));
    TestFalse(TEXT("profile 1 still unused"), IsProfileUsed(prefs, 1));

    // Raw bits: profile 0 always, an enabled profile without its used bit, a used disabled profile
    FOverlayPrefs raw = MakeDefaultPrefs();
    raw.UsedProfiles = 0x02;
    raw.EnabledProfiles = 0x09;
    TestTrue(TEXT("raw: profile 0 used without its bit"), IsProfileUsed(raw, 0));
    TestTrue(TEXT("raw: profile 1 used by its bit"), IsProfileUsed(raw, 1));
    TestFalse(TEXT("raw: profile 2 unused"), IsProfileUsed(raw, 2));
    TestTrue(TEXT("raw: enabled profile 3 used"), IsProfileUsed(raw, 3));
    TestFalse(TEXT("raw: profile 4 unused"), IsProfileUsed(raw, 4));

    // Out of range, even with every bit set
    raw.UsedProfiles = 0xFF;
    raw.EnabledProfiles = 0xFF;
    TestFalse(TEXT("-1 unused"), IsProfileUsed(raw, -1));
    TestFalse(TEXT("5 unused"), IsProfileUsed(raw, 5));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelFirstEnableTest, "SOLTest.OverlayModel.SetProfileEnabledFirst",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The first enable of a profile copies profile 0's mask, master and orbit switch into it (D8, D12); later edits of the
// new profile leave profile 0 alone; enabling twice changes nothing; any profile 1-4 may be enabled alone
bool FSOLOverlayModelFirstEnableTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;
    FOverlayPrefs prefs = MakeDefaultPrefs();

    // Edit profile 0 (active), and leave stale content in the unused slot 2
    SetEnabled(prefs, EOverlay::IconMoons, false);
    SetEnabled(prefs, EOverlay::AsteroidOrbits, true);
    SetMasterOn(prefs, false);
    SetOrbitLinesOn(prefs, false);
    prefs.Masks[2] = 0x3u;
    const uint32 mask0 = prefs.Masks[0];

    // Enable profile 2: a copy of profile 0
    TestTrue(TEXT("enable 2 changes"), SetProfileEnabled(prefs, 2, true));
    TestTrue(TEXT("2 enabled"), IsProfileEnabled(prefs, 2));
    TestTrue(TEXT("2 used"), IsProfileUsed(prefs, 2));
    TestEqual(TEXT("2 mask copied from 0"), prefs.Masks[2], mask0);
    TestFalse(TEXT("2 master copied (off)"), OverlayModelHasBit(prefs.MasterOnProfiles, 2));
    TestFalse(TEXT("2 orbit switch copied (off)"), OverlayModelHasBit(prefs.OrbitLinesProfiles, 2));
    TestEqual(TEXT("active stays 0"), prefs.ActiveProfile, 0);
    for (const int32 other : { 1, 3, 4 })
    {
        TestFalse(FString::Printf(TEXT("%d still disabled"), other), IsProfileEnabled(prefs, other));
        TestFalse(FString::Printf(TEXT("%d still unused"), other), IsProfileUsed(prefs, other));
    }

    // Editing profile 2 leaves profile 0 alone
    TestTrue(TEXT("activate 2"), SetActiveProfile(prefs, 2));
    SetEnabled(prefs, EOverlay::IconStars, false);
    SetMasterOn(prefs, true);
    SetOrbitLinesOn(prefs, true);
    TestEqual(TEXT("0 mask untouched"), prefs.Masks[0], mask0);
    TestFalse(TEXT("0 master untouched (off)"), OverlayModelHasBit(prefs.MasterOnProfiles, 0));
    TestFalse(TEXT("0 orbit switch untouched (off)"), OverlayModelHasBit(prefs.OrbitLinesProfiles, 0));
    TestEqual(TEXT("2 mask edited"), prefs.Masks[2], mask0 & ~OverlayModelBit(EOverlay::IconStars));

    // Enabling twice: no change, the edits stay
    const FOverlayPrefs edited = prefs;
    TestFalse(TEXT("enable 2 again changes nothing"), SetProfileEnabled(prefs, 2, true));
    OverlayModelCheckSamePrefs(*this, prefs, edited, TEXT("after the second enable"));

    // The copy comes from profile 0, not from the active profile
    TestTrue(TEXT("enable 3 changes"), SetProfileEnabled(prefs, 3, true));
    TestEqual(TEXT("3 mask copied from 0, not the active 2"), prefs.Masks[3], mask0);
    TestFalse(TEXT("3 master copied from 0 (off)"), OverlayModelHasBit(prefs.MasterOnProfiles, 3));
    TestFalse(TEXT("3 orbit switch copied from 0 (off)"), OverlayModelHasBit(prefs.OrbitLinesProfiles, 3));
    TestEqual(TEXT("active stays 2"), prefs.ActiveProfile, 2);

    // Profile 4 may be enabled while 1-3 are off (D12)
    FOverlayPrefs alone = MakeDefaultPrefs();
    TestTrue(TEXT("enable 4 alone"), SetProfileEnabled(alone, 4, true));
    TestTrue(TEXT("4 enabled"), IsProfileEnabled(alone, 4));
    TestEqual(TEXT("only 0 and 4 enabled"), static_cast<int32>(alone.EnabledProfiles), 0x11);
    TestEqual(TEXT("4 holds the defaults"), alone.Masks[4], DefaultMask());
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelReEnableTest, "SOLTest.OverlayModel.SetProfileEnabledReEnable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Re-enabling a used profile restores its own last mask, master and orbit switch, not a copy of profile 0 (D12)
bool FSOLOverlayModelReEnableTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;
    FOverlayPrefs prefs = MakeDefaultPrefs();

    // Enable and edit profile 1
    TestTrue(TEXT("enable 1"), SetProfileEnabled(prefs, 1, true));
    TestTrue(TEXT("activate 1"), SetActiveProfile(prefs, 1));
    SetEnabled(prefs, EOverlay::IconRings, false);
    SetEnabled(prefs, EOverlay::AsteroidOrbits, true);
    SetMasterOn(prefs, false);
    SetOrbitLinesOn(prefs, false);
    const uint32 mask1 = prefs.Masks[1];

    // Disable it: its content stays
    TestTrue(TEXT("disable 1"), SetProfileEnabled(prefs, 1, false));
    TestEqual(TEXT("active falls back to 0"), prefs.ActiveProfile, 0);
    TestTrue(TEXT("1 still used"), IsProfileUsed(prefs, 1));
    TestEqual(TEXT("1 mask kept"), prefs.Masks[1], mask1);
    TestFalse(TEXT("1 master kept (off)"), OverlayModelHasBit(prefs.MasterOnProfiles, 1));
    TestFalse(TEXT("1 orbit switch kept (off)"), OverlayModelHasBit(prefs.OrbitLinesProfiles, 1));

    // Edit profile 0, then re-enable 1
    SetEnabled(prefs, EOverlay::IconStars, false);
    TestTrue(TEXT("profiles 0 and 1 differ"), prefs.Masks[0] != mask1);
    TestTrue(TEXT("re-enable 1"), SetProfileEnabled(prefs, 1, true));
    TestTrue(TEXT("1 enabled"), IsProfileEnabled(prefs, 1));
    TestEqual(TEXT("1 mask is its own"), prefs.Masks[1], mask1);
    TestFalse(TEXT("1 master is its own (off)"), OverlayModelHasBit(prefs.MasterOnProfiles, 1));
    TestFalse(TEXT("1 orbit switch is its own (off)"), OverlayModelHasBit(prefs.OrbitLinesProfiles, 1));
    TestTrue(TEXT("0 master untouched (on)"), OverlayModelHasBit(prefs.MasterOnProfiles, 0));
    TestTrue(TEXT("0 orbit switch untouched (on)"), OverlayModelHasBit(prefs.OrbitLinesProfiles, 0));
    TestEqual(TEXT("active stays 0"), prefs.ActiveProfile, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelDisableTest, "SOLTest.OverlayModel.SetProfileEnabledDisable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Disabling clears only the enabled bit and moves the active profile to 0 when it was active; profile 0 and
// out-of-range profiles never change
bool FSOLOverlayModelDisableTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;
    FOverlayPrefs prefs = MakeDefaultPrefs();
    SetProfileEnabled(prefs, 1, true);
    SetProfileEnabled(prefs, 2, true);
    SetProfileEnabled(prefs, 3, true);

    // Disabling the active profile falls back to profile 0
    TestTrue(TEXT("activate 3"), SetActiveProfile(prefs, 3));
    TestTrue(TEXT("disable active 3"), SetProfileEnabled(prefs, 3, false));
    TestEqual(TEXT("active becomes 0"), prefs.ActiveProfile, 0);
    TestFalse(TEXT("3 disabled"), IsProfileEnabled(prefs, 3));
    TestTrue(TEXT("3 used"), IsProfileUsed(prefs, 3));

    // Disabling another profile keeps the active one
    TestTrue(TEXT("activate 2"), SetActiveProfile(prefs, 2));
    TestTrue(TEXT("disable 1"), SetProfileEnabled(prefs, 1, false));
    TestEqual(TEXT("active stays 2"), prefs.ActiveProfile, 2);
    TestFalse(TEXT("1 disabled"), IsProfileEnabled(prefs, 1));
    TestTrue(TEXT("2 still enabled"), IsProfileEnabled(prefs, 2));

    // Disabling a disabled profile changes nothing
    const FOverlayPrefs before = prefs;
    TestFalse(TEXT("disable 1 again"), SetProfileEnabled(prefs, 1, false));
    OverlayModelCheckSamePrefs(*this, prefs, before, TEXT("after disabling a disabled profile"));

    // Profile 0 cannot be disabled (or re-enabled)
    TestFalse(TEXT("disable 0"), SetProfileEnabled(prefs, 0, false));
    TestTrue(TEXT("0 still enabled"), IsProfileEnabled(prefs, 0));
    TestTrue(TEXT("0 bit still set"), OverlayModelHasBit(prefs.EnabledProfiles, 0));
    TestFalse(TEXT("enable 0"), SetProfileEnabled(prefs, 0, true));
    OverlayModelCheckSamePrefs(*this, prefs, before, TEXT("after touching profile 0"));

    // Out of range: no change
    TestFalse(TEXT("enable -1"), SetProfileEnabled(prefs, -1, true));
    TestFalse(TEXT("disable -1"), SetProfileEnabled(prefs, -1, false));
    TestFalse(TEXT("enable 5"), SetProfileEnabled(prefs, 5, true));
    TestFalse(TEXT("disable 5"), SetProfileEnabled(prefs, 5, false));
    OverlayModelCheckSamePrefs(*this, prefs, before, TEXT("after out-of-range calls"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelSetActiveTest, "SOLTest.OverlayModel.SetActiveProfile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Only an enabled profile can become active; the call returns whether the active profile changed
bool FSOLOverlayModelSetActiveTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;
    FOverlayPrefs prefs = MakeDefaultPrefs();
    SetProfileEnabled(prefs, 2, true);
    SetProfileEnabled(prefs, 3, true);
    SetProfileEnabled(prefs, 3, false);

    TestFalse(TEXT("disabled 1 refused"), SetActiveProfile(prefs, 1));
    TestEqual(TEXT("still 0 after 1"), prefs.ActiveProfile, 0);
    TestFalse(TEXT("used but disabled 3 refused"), SetActiveProfile(prefs, 3));
    TestEqual(TEXT("still 0 after 3"), prefs.ActiveProfile, 0);
    TestFalse(TEXT("-1 refused"), SetActiveProfile(prefs, -1));
    TestFalse(TEXT("5 refused"), SetActiveProfile(prefs, 5));
    TestEqual(TEXT("still 0 after out of range"), prefs.ActiveProfile, 0);

    TestTrue(TEXT("enabled 2 accepted"), SetActiveProfile(prefs, 2));
    TestEqual(TEXT("active 2"), prefs.ActiveProfile, 2);
    TestFalse(TEXT("2 again is no change"), SetActiveProfile(prefs, 2));
    TestEqual(TEXT("still 2"), prefs.ActiveProfile, 2);
    TestTrue(TEXT("back to 0"), SetActiveProfile(prefs, 0));
    TestEqual(TEXT("active 0"), prefs.ActiveProfile, 0);
    TestFalse(TEXT("0 again is no change"), SetActiveProfile(prefs, 0));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelCycleTest, "SOLTest.OverlayModel.CycleActiveProfile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Cycling makes the next enabled profile active, wrapping back to 0, and returns it
bool FSOLOverlayModelCycleTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;

    // Only profile 0: stays 0
    FOverlayPrefs only = MakeDefaultPrefs();
    TestEqual(TEXT("{0}: returns 0"), CycleActiveProfile(only), 0);
    TestEqual(TEXT("{0}: active 0"), only.ActiveProfile, 0);
    TestEqual(TEXT("{0}: again 0"), CycleActiveProfile(only), 0);

    // Profiles 0 and 1
    FOverlayPrefs two = MakeDefaultPrefs();
    SetProfileEnabled(two, 1, true);
    TestEqual(TEXT("{0,1}: 0 -> 1"), CycleActiveProfile(two), 1);
    TestEqual(TEXT("{0,1}: active 1"), two.ActiveProfile, 1);
    TestEqual(TEXT("{0,1}: 1 -> 0"), CycleActiveProfile(two), 0);
    TestEqual(TEXT("{0,1}: active 0"), two.ActiveProfile, 0);

    // Profiles 0, 2 and 4
    FOverlayPrefs gaps = MakeDefaultPrefs();
    SetProfileEnabled(gaps, 2, true);
    SetProfileEnabled(gaps, 4, true);
    TestEqual(TEXT("{0,2,4}: 0 -> 2"), CycleActiveProfile(gaps), 2);
    TestEqual(TEXT("{0,2,4}: 2 -> 4"), CycleActiveProfile(gaps), 4);
    TestEqual(TEXT("{0,2,4}: active 4"), gaps.ActiveProfile, 4);
    TestEqual(TEXT("{0,2,4}: 4 -> 0"), CycleActiveProfile(gaps), 0);
    TestEqual(TEXT("{0,2,4}: active 0"), gaps.ActiveProfile, 0);

    // From a corrupt active profile that is disabled: the next enabled one
    FOverlayPrefs corrupt = gaps;
    corrupt.ActiveProfile = 3;
    TestEqual(TEXT("corrupt 3 -> 4"), CycleActiveProfile(corrupt), 4);
    TestEqual(TEXT("corrupt 3: active 4"), corrupt.ActiveProfile, 4);
    corrupt.ActiveProfile = 1;
    TestEqual(TEXT("corrupt 1 -> 2"), CycleActiveProfile(corrupt), 2);
    TestEqual(TEXT("corrupt 1: active 2"), corrupt.ActiveProfile, 2);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelMasterTest, "SOLTest.OverlayModel.MasterPerProfile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The master O state belongs to the active profile (D4, D10): changing it in one profile leaves the others alone
bool FSOLOverlayModelMasterTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;
    FOverlayPrefs prefs = MakeDefaultPrefs();
    SetProfileEnabled(prefs, 1, true);
    TestTrue(TEXT("profile 0 master on"), IsMasterOn(prefs));

    // Toggle in profile 1
    TestTrue(TEXT("activate 1"), SetActiveProfile(prefs, 1));
    TestTrue(TEXT("profile 1 master starts on"), IsMasterOn(prefs));
    TestFalse(TEXT("toggle returns off"), ToggleMaster(prefs));
    TestFalse(TEXT("profile 1 master off"), IsMasterOn(prefs));
    TestFalse(TEXT("bit 1 cleared"), OverlayModelHasBit(prefs.MasterOnProfiles, 1));
    TestTrue(TEXT("bit 0 kept"), OverlayModelHasBit(prefs.MasterOnProfiles, 0));
    for (int32 profile = 2; profile < SOL::Overlay::MAX_PROFILES; ++profile)
    {
        TestTrue(FString::Printf(TEXT("bit %d kept"), profile), OverlayModelHasBit(prefs.MasterOnProfiles, profile));
    }

    // Cycling back and forth restores each profile's own master
    TestEqual(TEXT("cycle to 0"), CycleActiveProfile(prefs), 0);
    TestTrue(TEXT("profile 0 still on"), IsMasterOn(prefs));
    TestEqual(TEXT("cycle to 1"), CycleActiveProfile(prefs), 1);
    TestFalse(TEXT("profile 1 still off"), IsMasterOn(prefs));

    // SetMasterOn writes the active profile only
    SetMasterOn(prefs, false);
    TestFalse(TEXT("setting off twice keeps it off"), IsMasterOn(prefs));
    SetMasterOn(prefs, true);
    TestTrue(TEXT("set on"), IsMasterOn(prefs));
    TestFalse(TEXT("toggle off"), ToggleMaster(prefs));
    TestTrue(TEXT("toggle back on"), ToggleMaster(prefs));
    SetActiveProfile(prefs, 0);
    SetMasterOn(prefs, false);
    TestFalse(TEXT("profile 0 off"), IsMasterOn(prefs));
    TestTrue(TEXT("profile 1 bit untouched"), OverlayModelHasBit(prefs.MasterOnProfiles, 1));
    TestFalse(TEXT("profile 0 bit cleared"), OverlayModelHasBit(prefs.MasterOnProfiles, 0));

    // The getter reads the active profile's bit
    FOverlayPrefs raw = MakeDefaultPrefs();
    raw.EnabledProfiles = 0x05;
    raw.MasterOnProfiles = 0x01;
    raw.ActiveProfile = 2;
    TestFalse(TEXT("raw: profile 2 off"), IsMasterOn(raw));
    raw.ActiveProfile = 0;
    TestTrue(TEXT("raw: profile 0 on"), IsMasterOn(raw));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelOrbitLinesTest, "SOLTest.OverlayModel.OrbitLinesPerProfile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The Alt+O orbit-lines switch belongs to the active profile (D18): changing it in one profile leaves the others alone
bool FSOLOverlayModelOrbitLinesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;
    FOverlayPrefs prefs = MakeDefaultPrefs();
    SetProfileEnabled(prefs, 1, true);
    TestTrue(TEXT("profile 0 orbit lines on"), AreOrbitLinesOn(prefs));

    // Toggle in profile 1
    TestTrue(TEXT("activate 1"), SetActiveProfile(prefs, 1));
    TestTrue(TEXT("profile 1 orbit lines start on"), AreOrbitLinesOn(prefs));
    TestFalse(TEXT("toggle returns off"), ToggleOrbitLines(prefs));
    TestFalse(TEXT("profile 1 orbit lines off"), AreOrbitLinesOn(prefs));
    TestFalse(TEXT("bit 1 cleared"), OverlayModelHasBit(prefs.OrbitLinesProfiles, 1));
    TestTrue(TEXT("bit 0 kept"), OverlayModelHasBit(prefs.OrbitLinesProfiles, 0));
    for (int32 profile = 2; profile < SOL::Overlay::MAX_PROFILES; ++profile)
    {
        TestTrue(FString::Printf(TEXT("bit %d kept"), profile),
            OverlayModelHasBit(prefs.OrbitLinesProfiles, profile));
    }
    TestTrue(TEXT("the master is a separate switch"), IsMasterOn(prefs));

    // Cycling back and forth restores each profile's own switch
    TestEqual(TEXT("cycle to 0"), CycleActiveProfile(prefs), 0);
    TestTrue(TEXT("profile 0 still on"), AreOrbitLinesOn(prefs));
    TestEqual(TEXT("cycle to 1"), CycleActiveProfile(prefs), 1);
    TestFalse(TEXT("profile 1 still off"), AreOrbitLinesOn(prefs));

    // SetOrbitLinesOn writes the active profile only
    SetOrbitLinesOn(prefs, false);
    TestFalse(TEXT("setting off twice keeps it off"), AreOrbitLinesOn(prefs));
    SetOrbitLinesOn(prefs, true);
    TestTrue(TEXT("set on"), AreOrbitLinesOn(prefs));
    TestFalse(TEXT("toggle off"), ToggleOrbitLines(prefs));
    TestTrue(TEXT("toggle back on"), ToggleOrbitLines(prefs));
    SetActiveProfile(prefs, 0);
    SetOrbitLinesOn(prefs, false);
    TestFalse(TEXT("profile 0 off"), AreOrbitLinesOn(prefs));
    TestTrue(TEXT("profile 1 bit untouched"), OverlayModelHasBit(prefs.OrbitLinesProfiles, 1));
    TestFalse(TEXT("profile 0 bit cleared"), OverlayModelHasBit(prefs.OrbitLinesProfiles, 0));

    // The getter reads the active profile's bit
    FOverlayPrefs raw = MakeDefaultPrefs();
    raw.EnabledProfiles = 0x05;
    raw.OrbitLinesProfiles = 0x01;
    raw.ActiveProfile = 2;
    TestFalse(TEXT("raw: profile 2 off"), AreOrbitLinesOn(raw));
    raw.ActiveProfile = 0;
    TestTrue(TEXT("raw: profile 0 on"), AreOrbitLinesOn(raw));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelShownTest, "SOLTest.OverlayModel.IsShown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The active profile's master hides every row (Darkening included) and restores exactly the same set; unchecking Icons
// hides every icon category while they stay checked; each profile has its own master
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
        shownBefore.Add(IsShown(prefs, overlay));
        TestEqual(FString::Printf(TEXT("%s shown = checked"), GetDef(index).Id), shownBefore.Last(), bExpected);
    }
    TestFalse(TEXT("ships hidden"), IsShown(prefs, EOverlay::IconShips));
    TestFalse(TEXT("asteroid orbits hidden by default"), IsShown(prefs, EOverlay::AsteroidOrbits));
    TestTrue(TEXT("darkening shown"), IsShown(prefs, EOverlay::Darkening));

    // Master off hides every row without changing any checkbox
    const uint32 maskBefore = prefs.Masks[0];
    SetMasterOn(prefs, false);
    for (int32 index = 0; index < OVERLAY_COUNT; ++index)
    {
        TestFalse(FString::Printf(TEXT("%s hidden by the master"), GetDef(index).Id),
            IsShown(prefs, static_cast<EOverlay>(index)));
    }
    TestFalse(TEXT("darkening hidden by the master"), IsShown(prefs, EOverlay::Darkening));
    TestEqual(TEXT("master leaves the mask alone"), prefs.Masks[0], maskBefore);

    // Master back on shows exactly the same set
    SetMasterOn(prefs, true);
    for (int32 index = 0; index < OVERLAY_COUNT; ++index)
    {
        TestEqual(FString::Printf(TEXT("%s remembered"), GetDef(index).Id),
            IsShown(prefs, static_cast<EOverlay>(index)), shownBefore[index]);
    }

    // Unchecking Icons hides every category but keeps their checkboxes
    SetEnabled(prefs, EOverlay::Icons, false);
    for (const EOverlay category : OverlayModelIconCategories())
    {
        TestFalse(FString::Printf(TEXT("%s hidden with Icons off"), GetDef(static_cast<int32>(category)).Id),
            IsShown(prefs, category));
    }
    TestTrue(TEXT("stars still checked"), IsEnabled(prefs, EOverlay::IconStars));
    TestTrue(TEXT("planet orbits (top-level) still shown"), IsShown(prefs, EOverlay::PlanetOrbits));
    TestTrue(TEXT("radar ships (top-level) still shown"), IsShown(prefs, EOverlay::ShipsOnRadar));
    SetEnabled(prefs, EOverlay::Icons, true);
    TestTrue(TEXT("stars back with Icons"), IsShown(prefs, EOverlay::IconStars));
    TestFalse(TEXT("ships stay unchecked"), IsShown(prefs, EOverlay::IconShips));
    TestFalse(TEXT("Count never shown"), IsShown(prefs, EOverlay::Count));
    SetMasterOn(prefs, false);
    TestFalse(TEXT("Count never shown with the master off"), IsShown(prefs, EOverlay::Count));
    SetMasterOn(prefs, true);

    // The master is per profile: profile 1 off hides there while profile 0 still shows
    SetProfileEnabled(prefs, 1, true);
    SetActiveProfile(prefs, 1);
    SetMasterOn(prefs, false);
    TestFalse(TEXT("profile 1: planet orbits hidden"), IsShown(prefs, EOverlay::PlanetOrbits));
    TestFalse(TEXT("profile 1: stars hidden"), IsShown(prefs, EOverlay::IconStars));
    SetActiveProfile(prefs, 0);
    TestTrue(TEXT("profile 0: planet orbits shown"), IsShown(prefs, EOverlay::PlanetOrbits));
    TestTrue(TEXT("profile 0: stars shown"), IsShown(prefs, EOverlay::IconStars));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelOrbitShownTest, "SOLTest.OverlayModel.OrbitShown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Only the four orbit rows are orbit overlays, and their lines need the active profile's master, the row's checkbox
// and the active profile's orbit-lines switch
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

    FOverlayPrefs prefs = MakeDefaultPrefs();
    TestTrue(TEXT("planet orbits shown"), IsOrbitShown(prefs, EOverlay::PlanetOrbits));
    TestTrue(TEXT("moon orbits shown"), IsOrbitShown(prefs, EOverlay::MoonOrbits));
    TestTrue(TEXT("other orbits shown"), IsOrbitShown(prefs, EOverlay::OtherOrbits));
    TestFalse(TEXT("asteroid orbits unchecked by default"), IsOrbitShown(prefs, EOverlay::AsteroidOrbits));
    TestFalse(TEXT("a non-orbit row is never an orbit line"), IsOrbitShown(prefs, EOverlay::Icons));
    TestFalse(TEXT("darkening is no orbit line"), IsOrbitShown(prefs, EOverlay::Darkening));
    TestFalse(TEXT("Count is no orbit line"), IsOrbitShown(prefs, EOverlay::Count));

    // The active profile's orbit switch and master
    SetOrbitLinesOn(prefs, false);
    TestFalse(TEXT("orbit switch off hides"), IsOrbitShown(prefs, EOverlay::PlanetOrbits));
    SetOrbitLinesOn(prefs, true);
    SetMasterOn(prefs, false);
    TestFalse(TEXT("master off hides"), IsOrbitShown(prefs, EOverlay::PlanetOrbits));
    SetMasterOn(prefs, true);
    TestTrue(TEXT("both back on shows"), IsOrbitShown(prefs, EOverlay::PlanetOrbits));

    FOverlayPrefs withAsteroids = prefs;
    SetEnabled(withAsteroids, EOverlay::AsteroidOrbits, true);
    TestTrue(TEXT("checked asteroid orbits shown"), IsOrbitShown(withAsteroids, EOverlay::AsteroidOrbits));
    SetEnabled(withAsteroids, EOverlay::PlanetOrbits, false);
    TestFalse(TEXT("unchecked planet orbits hidden"), IsOrbitShown(withAsteroids, EOverlay::PlanetOrbits));

    // Per profile: profile 1's switch off hides its lines while profile 0 still shows them
    SetProfileEnabled(prefs, 1, true);
    SetActiveProfile(prefs, 1);
    SetOrbitLinesOn(prefs, false);
    TestFalse(TEXT("profile 1: planet orbits hidden"), IsOrbitShown(prefs, EOverlay::PlanetOrbits));
    TestTrue(TEXT("profile 1: the row is still shown as an overlay"), IsShown(prefs, EOverlay::PlanetOrbits));
    SetActiveProfile(prefs, 0);
    TestTrue(TEXT("profile 0: planet orbits shown"), IsOrbitShown(prefs, EOverlay::PlanetOrbits));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelSanitizedTest, "SOLTest.OverlayModel.Sanitized",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Sanitized strips garbage profile bits, forces profile 0 on, marks enabled profiles used, keeps the active profile
// enabled, drops invalid row bits, resets unused slots and keeps used disabled slots (SDD 34 section 4.3)
bool FSOLOverlayModelSanitizedTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;
    const int32 maxProfiles = SOL::Overlay::MAX_PROFILES;

    // Garbage bits above the five profiles are stripped; invalid row bits dropped
    FOverlayPrefs garbage;
    for (int32 slot = 0; slot < maxProfiles; ++slot)
    {
        garbage.Masks[slot] = 0xFFFFFFFFu;
    }
    garbage.EnabledProfiles = 0xFF;
    garbage.UsedProfiles = 0xFF;
    garbage.MasterOnProfiles = 0xEA;
    garbage.OrbitLinesProfiles = 0xF5;
    garbage.ActiveProfile = 2;
    const FOverlayPrefs garbageOut = Sanitized(garbage);
    TestEqual(TEXT("garbage: enabled"), static_cast<int32>(garbageOut.EnabledProfiles), 0x1F);
    TestEqual(TEXT("garbage: used"), static_cast<int32>(garbageOut.UsedProfiles), 0x1F);
    TestEqual(TEXT("garbage: master"), static_cast<int32>(garbageOut.MasterOnProfiles), 0x0A);
    TestEqual(TEXT("garbage: orbit lines"), static_cast<int32>(garbageOut.OrbitLinesProfiles), 0x15);
    TestEqual(TEXT("garbage: active kept"), garbageOut.ActiveProfile, 2);
    for (int32 slot = 0; slot < maxProfiles; ++slot)
    {
        TestEqual(FString::Printf(TEXT("garbage: slot %d valid bits"), slot), garbageOut.Masks[slot],
            AllBitsMask());
    }

    // Profile 0 is forced enabled and used
    FOverlayPrefs noZero = MakeDefaultPrefs();
    noZero.EnabledProfiles = 0;
    noZero.UsedProfiles = 0;
    const FOverlayPrefs noZeroOut = Sanitized(noZero);
    TestEqual(TEXT("bit 0 forced: enabled"), static_cast<int32>(noZeroOut.EnabledProfiles), 0x01);
    TestEqual(TEXT("bit 0 forced: used"), static_cast<int32>(noZeroOut.UsedProfiles), 0x01);

    // The active profile: clamped to [0, 4] and moved to 0 when that profile is disabled
    FOverlayPrefs active = MakeDefaultPrefs();
    active.EnabledProfiles = 0x09;
    active.UsedProfiles = 0x09;
    active.ActiveProfile = 3;
    TestEqual(TEXT("enabled 3 stays active"), Sanitized(active).ActiveProfile, 3);
    active.EnabledProfiles = 0x01;
    TestEqual(TEXT("disabled 3 -> 0"), Sanitized(active).ActiveProfile, 0);
    active.ActiveProfile = -3;
    TestEqual(TEXT("negative -> 0"), Sanitized(active).ActiveProfile, 0);
    active.ActiveProfile = 99;
    TestEqual(TEXT("99 with 4 disabled -> 0"), Sanitized(active).ActiveProfile, 0);
    active.EnabledProfiles = 0x11;
    active.UsedProfiles = 0x11;
    TestEqual(TEXT("99 with 4 enabled -> 4"), Sanitized(active).ActiveProfile, 4);

    // An unused slot is reset to the defaults with its master and orbit bits on
    FOverlayPrefs unused = MakeDefaultPrefs();
    unused.Masks[3] = 0x5u;
    unused.MasterOnProfiles = 0x17;
    unused.OrbitLinesProfiles = 0x17;
    const FOverlayPrefs unusedOut = Sanitized(unused);
    TestEqual(TEXT("unused slot 3: default mask"), unusedOut.Masks[3], DefaultMask());
    TestTrue(TEXT("unused slot 3: master on"), OverlayModelHasBit(unusedOut.MasterOnProfiles, 3));
    TestTrue(TEXT("unused slot 3: orbit lines on"), OverlayModelHasBit(unusedOut.OrbitLinesProfiles, 3));
    TestFalse(TEXT("unused slot 3: still unused"), OverlayModelHasBit(unusedOut.UsedProfiles, 3));

    // A used but disabled slot keeps its content (D12)
    FOverlayPrefs usedDisabled = MakeDefaultPrefs();
    usedDisabled.UsedProfiles = 0x05;
    usedDisabled.Masks[2] = 0x5u;
    usedDisabled.MasterOnProfiles = 0x1B;
    usedDisabled.OrbitLinesProfiles = 0x1B;
    const FOverlayPrefs usedDisabledOut = Sanitized(usedDisabled);
    TestEqual(TEXT("used disabled slot 2: mask kept"), usedDisabledOut.Masks[2], 0x5u);
    TestFalse(TEXT("used disabled slot 2: master kept (off)"), OverlayModelHasBit(usedDisabledOut.MasterOnProfiles, 2));
    TestFalse(TEXT("used disabled slot 2: orbit lines kept (off)"),
        OverlayModelHasBit(usedDisabledOut.OrbitLinesProfiles, 2));
    TestTrue(TEXT("used disabled slot 2: still used"), OverlayModelHasBit(usedDisabledOut.UsedProfiles, 2));
    TestFalse(TEXT("used disabled slot 2: still disabled"), OverlayModelHasBit(usedDisabledOut.EnabledProfiles, 2));

    // An enabled slot without its used bit becomes used, so its content is kept
    FOverlayPrefs enabledNotUsed = MakeDefaultPrefs();
    enabledNotUsed.EnabledProfiles = 0x09;
    enabledNotUsed.UsedProfiles = 0x01;
    enabledNotUsed.Masks[3] = 0x7u;
    const FOverlayPrefs enabledNotUsedOut = Sanitized(enabledNotUsed);
    TestEqual(TEXT("enabled 3 marked used"), static_cast<int32>(enabledNotUsedOut.UsedProfiles), 0x09);
    TestEqual(TEXT("enabled 3 keeps its mask"), enabledNotUsedOut.Masks[3], 0x7u);

    // Valid prefs pass through, and sanitizing is idempotent
    const FOverlayPrefs defaults = MakeDefaultPrefs();
    OverlayModelCheckSamePrefs(*this, Sanitized(defaults), defaults, TEXT("defaults pass through"));
    OverlayModelCheckSamePrefs(*this, Sanitized(garbageOut), garbageOut, TEXT("idempotent (garbage)"));
    OverlayModelCheckSamePrefs(*this, Sanitized(usedDisabledOut), usedDisabledOut, TEXT("idempotent (used disabled)"));
    OverlayModelCheckSamePrefs(*this, Sanitized(unusedOut), unusedOut, TEXT("idempotent (unused)"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelAreEqualTest, "SOLTest.OverlayModel.AreEqual",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AreEqual compares sanitized prefs: unused slots and invalid bits are ignored; the enabled, used, master and orbit
// bits, the active profile and every used mask (enabled or not) are not
bool FSOLOverlayModelAreEqualTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;
    const FOverlayPrefs base = MakeDefaultPrefs();
    TestTrue(TEXT("equal to itself"), AreEqual(base, base));
    TestTrue(TEXT("equal to fresh defaults"), AreEqual(base, MakeDefaultPrefs()));

    // Ignored: an unused slot's mask, master and orbit bits, and invalid row bits
    FOverlayPrefs unusedSlot = base;
    unusedSlot.Masks[3] = 0x1u;
    unusedSlot.MasterOnProfiles = 0x17;
    unusedSlot.OrbitLinesProfiles = 0x17;
    TestTrue(TEXT("an unused slot is ignored"), AreEqual(base, unusedSlot));

    FOverlayPrefs invalidBit = base;
    invalidBit.Masks[0] |= 1u << 31;
    TestTrue(TEXT("an invalid bit is ignored"), AreEqual(base, invalidBit));

    // Corrupt values that sanitize to the defaults compare equal
    FOverlayPrefs corrupt = base;
    corrupt.EnabledProfiles = 0;
    corrupt.ActiveProfile = 7;
    TestTrue(TEXT("sanitized corrupt prefs equal the defaults"), AreEqual(corrupt, base));

    // Compared: a row bit, the master only, the orbit switch only
    FOverlayPrefs oneBit = base;
    Toggle(oneBit, EOverlay::IconRings);
    TestFalse(TEXT("one overlay bit differs"), AreEqual(base, oneBit));
    TestFalse(TEXT("symmetric"), AreEqual(oneBit, base));

    FOverlayPrefs masterOnly = base;
    SetMasterOn(masterOnly, false);
    TestFalse(TEXT("the master only differs"), AreEqual(base, masterOnly));
    TestFalse(TEXT("master: symmetric"), AreEqual(masterOnly, base));

    FOverlayPrefs orbitOnly = base;
    SetOrbitLinesOn(orbitOnly, false);
    TestFalse(TEXT("the orbit switch only differs"), AreEqual(base, orbitOnly));

    // Compared: enabled bits and the active profile
    FOverlayPrefs twoEnabled = base;
    SetProfileEnabled(twoEnabled, 1, true);
    TestFalse(TEXT("enabled profiles differ"), AreEqual(base, twoEnabled));

    FOverlayPrefs secondActive = twoEnabled;
    SetActiveProfile(secondActive, 1);
    TestFalse(TEXT("active profile differs"), AreEqual(twoEnabled, secondActive));

    // Compared: a used but disabled slot's content, and the used bit itself
    FOverlayPrefs usedDisabled = base;
    SetProfileEnabled(usedDisabled, 2, true);
    SetProfileEnabled(usedDisabled, 2, false);
    TestFalse(TEXT("a used slot differs from an unused one"), AreEqual(base, usedDisabled));
    FOverlayPrefs usedDisabledEdited = usedDisabled;
    usedDisabledEdited.Masks[2] = 0u;
    TestFalse(TEXT("a used disabled slot's mask differs"), AreEqual(usedDisabled, usedDisabledEdited));
    FOverlayPrefs usedDisabledMaster = usedDisabled;
    usedDisabledMaster.MasterOnProfiles = 0x1B;
    TestFalse(TEXT("a used disabled slot's master differs"), AreEqual(usedDisabled, usedDisabledMaster));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelSavedProfileTest, "SOLTest.OverlayModel.SavedProfile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A profile saves as every id -> checked and loads back case-insensitively; unknown ids (the old SpaceDust key
// included) are ignored and missing ids take their default
bool FSOLOverlayModelSavedProfileTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;

    // ToSavedProfile names every row once with its bit
    const uint32 mask = OverlayModelBit(EOverlay::Icons) | OverlayModelBit(EOverlay::IconMoons)
        | OverlayModelBit(EOverlay::AsteroidOrbits) | OverlayModelBit(EOverlay::Darkening);
    const TMap<FString, bool> saved = ToSavedProfile(mask);
    TestEqual(TEXT("one entry per row"), saved.Num(), OVERLAY_COUNT);
    TestEqual(TEXT("sixteen entries"), saved.Num(), OVERLAYMODEL_ROW_COUNT);
    TestNull(TEXT("no SpaceDust entry"), saved.Find(TEXT("SpaceDust")));
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

    // Issue #34: a stray SpaceDust key (any case, either value) in a saved profile is ignored
    for (const bool bDust : { false, true })
    {
        TMap<FString, bool> withDust = ToSavedProfile(mask);
        withDust.Add(TEXT("SpaceDust"), bDust);
        TestEqual(FString::Printf(TEXT("SpaceDust=%d ignored"), bDust ? 1 : 0), FromSavedProfile(withDust), mask);
    }
    TMap<FString, bool> dustOnly;
    dustOnly.Add(TEXT("spacedust"), false);
    TestEqual(TEXT("only a spacedust key -> default"), FromSavedProfile(dustOnly), DefaultMask());
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelLanesRowTest, "SOLTest.OverlayModel.LanesRow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Issue #38 (SDD 38 section 4.9, D5, D11, D17): the Lanes row is a top-level row, on by default, between Ships on
// Radar and Darkening, persisted by its id, governed by the master O, per profile and copied on first enable
bool FSOLOverlayModelLanesRowTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;
    const uint32 lanesBit = OverlayModelBit(EOverlay::Lanes);

    // The row itself and its place in the table
    TestEqual(TEXT("sixteen rows"), OVERLAY_COUNT, 16);
    TestEqual(TEXT("Lanes at index 14"), static_cast<int32>(EOverlay::Lanes), OVERLAYMODEL_LANES_INDEX);
    TestEqual(TEXT("Lanes right after Ships on Radar"), static_cast<int32>(EOverlay::Lanes),
        static_cast<int32>(EOverlay::ShipsOnRadar) + 1);
    TestEqual(TEXT("Darkening right after Lanes"), static_cast<int32>(EOverlay::Darkening),
        static_cast<int32>(EOverlay::Lanes) + 1);
    TestEqual(TEXT("Darkening still last"), static_cast<int32>(EOverlay::Darkening), OVERLAY_COUNT - 1);
    const FOverlayDef& def = GetDef(OVERLAYMODEL_LANES_INDEX);
    TestEqual(TEXT("Lanes id"), FString(def.Id), FString(TEXT("Lanes")));
    TestEqual(TEXT("Lanes label"), FString(def.Label), FString(TEXT("Lanes")));
    TestTrue(TEXT("Lanes on by default"), def.bDefaultOn);
    TestEqual(TEXT("Lanes is top level"), def.Parent, static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("FindById lanes"), FindById(TEXT("lanes")), OVERLAYMODEL_LANES_INDEX);
    TestEqual(TEXT("FindById Lanes"), FindById(TEXT("Lanes")), OVERLAYMODEL_LANES_INDEX);
    TestEqual(TEXT("the label NPC Lanes is not an id"), FindById(TEXT("NPC Lanes")), static_cast<int32>(INDEX_NONE));

    // On in the default mask and in every mask of the default prefs
    TestTrue(TEXT("Lanes in DefaultMask"), (DefaultMask() & lanesBit) != 0u);
    TestTrue(TEXT("Lanes in AllBitsMask"), (AllBitsMask() & lanesBit) != 0u);
    const FOverlayPrefs defaults = MakeDefaultPrefs();
    for (int32 slot = 0; slot < SOL::Overlay::MAX_PROFILES; ++slot)
    {
        TestTrue(FString::Printf(TEXT("default prefs slot %d has Lanes"), slot),
            (defaults.Masks[slot] & lanesBit) != 0u);
    }
    TestTrue(TEXT("Lanes enabled in the default prefs"), IsEnabled(defaults, EOverlay::Lanes));

    // Saved by id: 16 keys including Lanes, with the bit's state
    const TMap<FString, bool> savedOn = ToSavedProfile(DefaultMask());
    TestEqual(TEXT("sixteen saved keys"), savedOn.Num(), OVERLAYMODEL_ROW_COUNT);
    const bool* lanesOn = savedOn.Find(TEXT("Lanes"));
    if (TestNotNull(TEXT("Lanes key saved"), lanesOn))
    {
        TestTrue(TEXT("Lanes saved on"), *lanesOn);
    }
    const TMap<FString, bool> savedOff = ToSavedProfile(DefaultMask() & ~lanesBit);
    const bool* lanesOff = savedOff.Find(TEXT("Lanes"));
    if (TestNotNull(TEXT("Lanes key saved when off"), lanesOff))
    {
        TestFalse(TEXT("Lanes saved off"), *lanesOff);
    }

    // A v12 profile: the 15 pre-#38 keys (literal ids), no Lanes key -> Lanes on, every other row as in the map
    TMap<FString, bool> v12;
    v12.Add(TEXT("Icons"), true);
    v12.Add(TEXT("IconStars"), false);
    v12.Add(TEXT("IconPlanets"), true);
    v12.Add(TEXT("IconMoons"), false);
    v12.Add(TEXT("IconDwarfs"), true);
    v12.Add(TEXT("IconAsteroids"), false);
    v12.Add(TEXT("IconRings"), true);
    v12.Add(TEXT("IconShips"), false);
    v12.Add(TEXT("IconTargets"), true);
    v12.Add(TEXT("PlanetOrbits"), false);
    v12.Add(TEXT("AsteroidOrbits"), true);
    v12.Add(TEXT("MoonOrbits"), false);
    v12.Add(TEXT("OtherOrbits"), true);
    v12.Add(TEXT("ShipsOnRadar"), false);
    v12.Add(TEXT("Darkening"), false);
    TestEqual(TEXT("the v12 map holds fifteen keys"), v12.Num(), 15);           // Frozen: the rows a v12 file wrote
    uint32 expectedV12 = lanesBit;
    for (const TPair<FString, bool>& entry : v12)
    {
        const int32 row = FindById(entry.Key);
        if (TestTrue(FString::Printf(TEXT("v12 key %s is a row"), *entry.Key), row != INDEX_NONE) && entry.Value)
        {
            expectedV12 |= 1u << static_cast<uint32>(row);
        }
    }
    TestEqual(TEXT("v12 profile: Lanes on, the rest as saved"), FromSavedProfile(v12), expectedV12);

    // Only Lanes = false: Lanes off, every other row its default
    TMap<FString, bool> onlyOff;
    onlyOff.Add(TEXT("Lanes"), false);
    TestEqual(TEXT("{Lanes: false}"), FromSavedProfile(onlyOff), DefaultMask() & ~lanesBit);
    TMap<FString, bool> lowerOff;
    lowerOff.Add(TEXT("lanes"), false);
    TestEqual(TEXT("{lanes: false} (case-insensitive)"), FromSavedProfile(lowerOff), DefaultMask() & ~lanesBit);

    // Shown only with the active profile's master on and the row checked (D11)
    FOverlayPrefs prefs = MakeDefaultPrefs();
    TestTrue(TEXT("shown by default"), IsShown(prefs, EOverlay::Lanes));
    SetMasterOn(prefs, false);
    TestFalse(TEXT("hidden with the master off"), IsShown(prefs, EOverlay::Lanes));
    TestTrue(TEXT("still checked with the master off"), IsEnabled(prefs, EOverlay::Lanes));
    SetMasterOn(prefs, true);
    TestTrue(TEXT("shown again with the master on"), IsShown(prefs, EOverlay::Lanes));
    SetEnabled(prefs, EOverlay::Lanes, false);
    TestFalse(TEXT("hidden when unchecked"), IsShown(prefs, EOverlay::Lanes));
    SetEnabled(prefs, EOverlay::Lanes, true);

    // Not an orbit row, never an orbit line
    TestFalse(TEXT("Lanes is no orbit overlay"), IsOrbitOverlay(EOverlay::Lanes));
    TestFalse(TEXT("Lanes is no orbit line"), IsOrbitShown(prefs, EOverlay::Lanes));

    // Per profile: toggling Lanes in profile 2 leaves profiles 0 and 1 alone
    FOverlayPrefs perProfile = MakeDefaultPrefs();
    SetProfileEnabled(perProfile, 1, true);
    SetProfileEnabled(perProfile, 2, true);
    TestTrue(TEXT("activate 2"), SetActiveProfile(perProfile, 2));
    TestFalse(TEXT("toggle Lanes off in 2"), Toggle(perProfile, EOverlay::Lanes));
    TestEqual(TEXT("profile 2 lost the Lanes bit"), perProfile.Masks[2] & lanesBit, 0u);
    TestEqual(TEXT("profile 0 keeps the Lanes bit"), perProfile.Masks[0] & lanesBit, lanesBit);
    TestEqual(TEXT("profile 1 keeps the Lanes bit"), perProfile.Masks[1] & lanesBit, lanesBit);
    SetActiveProfile(perProfile, 1);
    TestTrue(TEXT("profile 1 shows Lanes"), IsShown(perProfile, EOverlay::Lanes));
    SetActiveProfile(perProfile, 2);
    TestFalse(TEXT("profile 2 hides Lanes"), IsShown(perProfile, EOverlay::Lanes));

    // The first enable copies profile 0's Lanes bit, off or on
    FOverlayPrefs copyOff = MakeDefaultPrefs();
    SetEnabled(copyOff, EOverlay::Lanes, false);
    TestTrue(TEXT("enable 3"), SetProfileEnabled(copyOff, 3, true));
    TestEqual(TEXT("copied Lanes off"), copyOff.Masks[3] & lanesBit, 0u);
    FOverlayPrefs copyOn = MakeDefaultPrefs();
    copyOn.Masks[4] = 0u;
    TestTrue(TEXT("enable 4"), SetProfileEnabled(copyOn, 4, true));
    TestEqual(TEXT("copied Lanes on"), copyOn.Masks[4] & lanesBit, lanesBit);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOverlayModelProfileTagAlphaTest, "SOLTest.OverlayModel.ProfileTagAlpha",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The HUD profile tag is fully bright for PROFILE_TAG_SHOW_S, then fades linearly to 0 over PROFILE_TAG_FADE_S; a
// negative or non-finite time gives 0
bool FSOLOverlayModelProfileTagAlphaTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;
    const double show = static_cast<double>(SOL::Overlay::PROFILE_TAG_SHOW_S);
    const double fade = static_cast<double>(SOL::Overlay::PROFILE_TAG_FADE_S);
    constexpr float tolerance = 1.0e-4f;

    TestEqual(TEXT("0 -> 1"), ProfileTagAlpha(0.0), 1.0f, tolerance);
    TestEqual(TEXT("half the show time -> 1"), ProfileTagAlpha(show * 0.5), 1.0f, tolerance);
    TestEqual(TEXT("SHOW -> 1"), ProfileTagAlpha(show), 1.0f, tolerance);
    TestEqual(TEXT("SHOW + FADE/4 -> 0.75"), ProfileTagAlpha(show + fade * 0.25), 0.75f, tolerance);
    TestEqual(TEXT("SHOW + FADE/2 -> 0.5"), ProfileTagAlpha(show + fade * 0.5), 0.5f, tolerance);
    TestEqual(TEXT("SHOW + FADE -> 0"), ProfileTagAlpha(show + fade), 0.0f, tolerance);
    TestEqual(TEXT("long after -> 0"), ProfileTagAlpha(show + fade + 10.0), 0.0f, tolerance);
    TestEqual(TEXT("-1 -> 0"), ProfileTagAlpha(-1.0), 0.0f, tolerance);
    TestEqual(TEXT("slightly negative -> 0"), ProfileTagAlpha(-1.0e-6), 0.0f, tolerance);
    TestEqual(TEXT("NaN -> 0"), ProfileTagAlpha(std::numeric_limits<double>::quiet_NaN()), 0.0f, tolerance);
    TestEqual(TEXT("+inf -> 0"), ProfileTagAlpha(std::numeric_limits<double>::infinity()), 0.0f, tolerance);
    TestEqual(TEXT("-inf -> 0"), ProfileTagAlpha(-std::numeric_limits<double>::infinity()), 0.0f, tolerance);

    // Never outside [0, 1], and never rising over time
    float previous = 1.0f;
    for (int32 step = 0; step <= 40; ++step)
    {
        const double t = (show + fade) * 1.2 * static_cast<double>(step) / 40.0;
        const float alpha = ProfileTagAlpha(t);
        TestTrue(FString::Printf(TEXT("in [0, 1] at %.3f s"), t), alpha >= 0.0f && alpha <= 1.0f);
        TestTrue(FString::Printf(TEXT("not rising at %.3f s"), t), alpha <= previous + tolerance);
        previous = alpha;
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
