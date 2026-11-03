/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSettingsRules.h"
#include "Menu/SOLSettingsSave.h"
#include "SOLConstants.h"

#include "GenericPlatform/GenericWindow.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    constexpr float SETTINGSRULES_TEST_NAN = std::numeric_limits<float>::quiet_NaN();
    constexpr float SETTINGSRULES_TEST_TOLERANCE = 1.0e-5f;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesNamesTest, "SOLTest.SettingsRules.Names",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Quality presets and window modes have their names, clamped at both ends
bool FSOLSettingsRulesNamesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsRules;
    TestEqual(TEXT("quality 0"), QualityName(0), FString(TEXT("Low")));
    TestEqual(TEXT("quality 3"), QualityName(3), FString(TEXT("Epic")));
    TestEqual(TEXT("quality 4"), QualityName(4), FString(TEXT("Cinematic")));
    TestEqual(TEXT("quality below"), QualityName(-1), FString(TEXT("Low")));
    TestEqual(TEXT("quality above"), QualityName(99), FString(TEXT("Cinematic")));
    TestEqual(TEXT("fullscreen = EWindowMode::Fullscreen"), WindowModeName(EWindowMode::Fullscreen),
        FString(TEXT("Fullscreen")));
    TestEqual(TEXT("borderless = EWindowMode::WindowedFullscreen"), WindowModeName(EWindowMode::WindowedFullscreen),
        FString(TEXT("Borderless window")));
    TestEqual(TEXT("windowed = EWindowMode::Windowed"), WindowModeName(EWindowMode::Windowed),
        FString(TEXT("Windowed")));
    TestEqual(TEXT("mode above"), WindowModeName(7), FString(TEXT("Windowed")));
    TestEqual(TEXT("mode below"), WindowModeName(-7), FString(TEXT("Fullscreen")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesFormatTest, "SOLTest.SettingsRules.Format",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Resolutions, percentages and multipliers read as the page shows them
bool FSOLSettingsRulesFormatTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsRules;
    TestEqual(TEXT("resolution"), FormatResolution(FIntPoint(1920, 1080)), FString(TEXT("1920 x 1080")));
    TestEqual(TEXT("percent"), FormatPercent(0.75f), FString(TEXT("75%")));
    TestEqual(TEXT("percent rounds"), FormatPercent(0.333f), FString(TEXT("33%")));
    TestEqual(TEXT("percent zero"), FormatPercent(0.0f), FString(TEXT("0%")));
    TestEqual(TEXT("percent full"), FormatPercent(1.0f), FString(TEXT("100%")));
    TestEqual(TEXT("percent NaN"), FormatPercent(SETTINGSRULES_TEST_NAN), FString(TEXT("0%")));
    TestEqual(TEXT("multiplier whole"), FormatMultiplier(1.0f), FString(TEXT("1x")));
    TestEqual(TEXT("multiplier two decimals"), FormatMultiplier(1.25f), FString(TEXT("1.25x")));
    TestEqual(TEXT("multiplier trailing zero"), FormatMultiplier(1.5f), FString(TEXT("1.5x")));
    TestEqual(TEXT("multiplier rounds"), FormatMultiplier(0.333f), FString(TEXT("0.33x")));
    TestEqual(TEXT("multiplier ten"), FormatMultiplier(10.0f), FString(TEXT("10x")));
    TestEqual(TEXT("multiplier NaN"), FormatMultiplier(SETTINGSRULES_TEST_NAN), FString(TEXT("0x")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesResolutionsTest, "SOLTest.SettingsRules.Resolutions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The resolution list is positive, unique, sorted, includes the current one, and steps without wrapping
bool FSOLSettingsRulesResolutionsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsRules;
    const TArray<FIntPoint> supported = { FIntPoint(1920, 1080), FIntPoint(1280, 720), FIntPoint(1920, 1080),
        FIntPoint(0, 600), FIntPoint(-1, -1), FIntPoint(1920, 1200), FIntPoint(1280, 1024) };
    const TArray<FIntPoint> choices = BuildResolutionChoices(supported, FIntPoint(1600, 900));
    const TArray<FIntPoint> expected = { FIntPoint(1280, 720), FIntPoint(1280, 1024), FIntPoint(1600, 900),
        FIntPoint(1920, 1080), FIntPoint(1920, 1200) };
    TestTrue(TEXT("sorted, unique, positive, with the current one"), choices == expected);
    TestEqual(TEXT("current already listed is not doubled"), BuildResolutionChoices(supported,
        FIntPoint(1280, 720)).Num(), 4);
    TestEqual(TEXT("invalid current is not added"), BuildResolutionChoices(supported, FIntPoint(0, 0)).Num(), 4);
    TestEqual(TEXT("nothing listed, only the current"), BuildResolutionChoices({}, FIntPoint(800, 600)).Num(), 1);
    TestEqual(TEXT("nothing at all"), BuildResolutionChoices({}, FIntPoint::ZeroValue).Num(), 0);

    TestEqual(TEXT("find"), FindResolution(choices, FIntPoint(1600, 900)), 2);
    TestEqual(TEXT("find missing"), FindResolution(choices, FIntPoint(640, 480)), INDEX_NONE);

    TestEqual(TEXT("step forward"), StepIndex(2, 5, 1), 3);
    TestEqual(TEXT("step back"), StepIndex(2, 5, -1), 1);
    TestEqual(TEXT("big direction is one step"), StepIndex(2, 5, 40), 3);
    TestEqual(TEXT("no step"), StepIndex(2, 5, 0), 2);
    TestEqual(TEXT("no wrap at the end"), StepIndex(4, 5, 1), 4);
    TestEqual(TEXT("no wrap at the start"), StepIndex(0, 5, -1), 0);
    TestEqual(TEXT("out-of-range index is clamped first"), StepIndex(9, 5, -1), 3);
    TestEqual(TEXT("negative index is clamped first"), StepIndex(-3, 5, 1), 1);
    TestEqual(TEXT("empty list"), StepIndex(0, 0, 1), INDEX_NONE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesSnapTest, "SOLTest.SettingsRules.Snap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Slider values clamp and snap to their step from the range's low end
bool FSOLSettingsRulesSnapTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsRules;
    TestEqual(TEXT("snaps down"), SnapToStep(0.52f, 0.05f, 0.0f, 1.0f), 0.5f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("snaps up"), SnapToStep(0.53f, 0.05f, 0.0f, 1.0f), 0.55f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("steps from the low end"), SnapToStep(1.02f, 0.05f, 0.1f, 5.0f), 1.0f,
        SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("clamps high"), SnapToStep(7.0f, 0.05f, 0.1f, 5.0f), 5.0f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("clamps low"), SnapToStep(-1.0f, 0.05f, 0.5f, 2.0f), 0.5f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("NaN is the low end"), SnapToStep(SETTINGSRULES_TEST_NAN, 0.05f, 0.5f, 2.0f), 0.5f,
        SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("no step only clamps"), SnapToStep(0.123f, 0.0f, 0.0f, 1.0f), 0.123f,
        SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("a step past the top stays at the top"), SnapToStep(0.99f, 0.3f, 0.0f, 1.0f), 0.9f,
        SETTINGSRULES_TEST_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesLookTest, "SOLTest.SettingsRules.Look",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Mouse sensitivity scales both axes; invert Y flips only Y
bool FSOLSettingsRulesLookTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsRules;
    const FVector2d delta(4.0, -2.0);
    TestTrue(TEXT("unit, not inverted"), ApplyLookSettings(delta, 1.0f, false).Equals(delta));
    TestTrue(TEXT("sensitivity 2"), ApplyLookSettings(delta, 2.0f, false).Equals(FVector2d(8.0, -4.0)));
    TestTrue(TEXT("inverted"), ApplyLookSettings(delta, 1.0f, true).Equals(FVector2d(4.0, 2.0)));
    TestTrue(TEXT("sensitivity 0.5 inverted"), ApplyLookSettings(delta, 0.5f, true).Equals(FVector2d(2.0, 1.0)));
    TestTrue(TEXT("zero delta"), ApplyLookSettings(FVector2d::ZeroVector, 3.0f, true).IsNearlyZero());
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesSaveVersionTest, "SOLTest.SettingsRules.SaveVersion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The schema version decides use / migrate / ignore, and the save object round-trips the values
bool FSOLSettingsRulesSaveVersionTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsRules;
    TestTrue(TEXT("same version"), ClassifySaveVersion(3, 3) == ESaveVersionAction::Use);
    TestTrue(TEXT("older version"), ClassifySaveVersion(2, 3) == ESaveVersionAction::Migrate);
    TestTrue(TEXT("oldest version"), ClassifySaveVersion(1, 3) == ESaveVersionAction::Migrate);
    TestTrue(TEXT("newer version"), ClassifySaveVersion(4, 3) == ESaveVersionAction::UseDefaults);
    TestTrue(TEXT("unversioned"), ClassifySaveVersion(0, 3) == ESaveVersionAction::UseDefaults);
    TestTrue(TEXT("negative"), ClassifySaveVersion(-1, 3) == ESaveVersionAction::UseDefaults);

    // The save object starts at the defaults and gives back exactly what was put in
    USOLSettingsSave* save = NewObject<USOLSettingsSave>();
    TestTrue(TEXT("new save holds the defaults"), SOLSettingsModel::AreEqual(save->GetValues(),
        SOLSettingsModel::FSettingsValues()));
    TestEqual(TEXT("new save is unversioned"), save->Version, 0);
    SOLSettingsModel::FSettingsValues values;
    values.MasterVolume = 0.3f;
    values.SfxVolume = 0.4f;
    values.MusicVolume = 0.5f;
    values.MouseSensitivity = 2.5f;
    values.bInvertY = true;
    values.HudScale = 1.5f;
    values.bHudVisible = false;
    values.bDefaultFlightAssist = false;
    values.bVsync = false;
    values.QualityPreset = 1;
    save->SetValues(values);
    TestTrue(TEXT("round trip"), SOLSettingsModel::AreEqual(save->GetValues(), values));

    // A save that predates the chosen flags (or a new one) reads them as not chosen, so its graphics never override
    // the engine at boot
    const USOLSettingsSave* fresh = NewObject<USOLSettingsSave>();
    TestFalse(TEXT("vsync not chosen by default"), fresh->bVsyncChosen);
    TestFalse(TEXT("quality not chosen by default"), fresh->bQualityChosen);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesBootGraphicsTest, "SOLTest.SettingsRules.BootGraphics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At boot only the vsync and quality the player chose are pushed, and an engine custom scalability mix (-1) survives
bool FSOLSettingsRulesBootGraphicsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsRules;
    constexpr int32 custom = -1;

    // No save: everything from the engine, nothing pushed; a custom engine mix shows the fallback preset
    FBootGraphics boot = DecideBootGraphics(false, false, false, true, 3, false, 2, 3);
    TestTrue(TEXT("no save: vsync from the engine"), boot.bVsync == false && !boot.bPushVsync);
    TestTrue(TEXT("no save: quality from the engine"), boot.QualityPreset == 2 && !boot.bPushQuality);
    boot = DecideBootGraphics(false, false, false, true, 3, true, custom, 3);
    TestTrue(TEXT("no save, custom engine mix: not pushed, fallback shown"), !boot.bPushQuality
        && boot.QualityPreset == 3);

    // The finding: a save written without the player touching quality (it holds the default 3) over a custom mix
    boot = DecideBootGraphics(true, false, false, true, 3, true, custom, 3);
    TestFalse(TEXT("unchosen saved quality never overwrites a custom mix"), boot.bPushQuality);
    TestFalse(TEXT("unchosen saved vsync is not pushed"), boot.bPushVsync);
    boot = DecideBootGraphics(true, false, false, false, 1, true, 4, 1);
    TestTrue(TEXT("unchosen values start from the engine"), boot.bVsync && boot.QualityPreset == 4
        && !boot.bPushVsync && !boot.bPushQuality);

    // Chosen values win and are pushed only where the engine differs
    boot = DecideBootGraphics(true, true, true, false, 1, true, 4, 1);
    TestTrue(TEXT("chosen values are the model's"), !boot.bVsync && boot.QualityPreset == 1);
    TestTrue(TEXT("chosen values that differ are pushed"), boot.bPushVsync && boot.bPushQuality);
    boot = DecideBootGraphics(true, true, true, true, 2, true, 2, 2);
    TestTrue(TEXT("chosen values equal to the engine's are not pushed"), !boot.bPushVsync && !boot.bPushQuality);
    boot = DecideBootGraphics(true, false, true, true, 0, true, custom, 0);
    TestTrue(TEXT("a chosen preset replaces a later custom mix"), boot.bPushQuality && boot.QualityPreset == 0);

    // Only the chosen field is pushed
    boot = DecideBootGraphics(true, true, false, false, 3, true, custom, 3);
    TestTrue(TEXT("chosen vsync alone"), boot.bPushVsync && !boot.bPushQuality);

    // A corrupt chosen preset is ignored (engine value kept); an ignored save chooses nothing
    boot = DecideBootGraphics(true, true, true, true, 9, true, 2, 9);
    TestTrue(TEXT("out-of-range chosen preset ignored"), !boot.bPushQuality && boot.QualityPreset == 2);
    boot = DecideBootGraphics(true, true, true, true, -3, true, custom, -3);
    TestTrue(TEXT("negative preset over custom: clamped fallback, not pushed"), !boot.bPushQuality
        && boot.QualityPreset == 0);
    boot = DecideBootGraphics(false, true, true, false, 1, true, 4, 1);
    TestTrue(TEXT("flags of an unused save are ignored"), boot.bVsync && boot.QualityPreset == 4 && !boot.bPushVsync
        && !boot.bPushQuality);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesOrbitLinesSaveTest, "SOLTest.SettingsRules.OrbitLinesSave",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Schema 2+ carries the orbit-lines flag: a new save shows them, and SetValues/GetValues round-trip both values
bool FSOLSettingsRulesOrbitLinesSaveTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsRules;
    TestEqual(TEXT("schema version is 8"), SOL::Settings::SAVE_VERSION, 8);
    TestTrue(TEXT("schema 1 migrates"), ClassifySaveVersion(1, SOL::Settings::SAVE_VERSION)
        == ESaveVersionAction::Migrate);
    TestTrue(TEXT("schema 2 migrates"), ClassifySaveVersion(2, SOL::Settings::SAVE_VERSION)
        == ESaveVersionAction::Migrate);
    TestTrue(TEXT("schema 3 migrates"), ClassifySaveVersion(3, SOL::Settings::SAVE_VERSION)
        == ESaveVersionAction::Migrate);
    TestTrue(TEXT("schema 4 migrates"), ClassifySaveVersion(4, SOL::Settings::SAVE_VERSION)
        == ESaveVersionAction::Migrate);
    TestTrue(TEXT("schema 5 migrates"), ClassifySaveVersion(5, SOL::Settings::SAVE_VERSION)
        == ESaveVersionAction::Migrate);
    TestTrue(TEXT("schema 6 migrates"), ClassifySaveVersion(6, SOL::Settings::SAVE_VERSION)
        == ESaveVersionAction::Migrate);
    TestTrue(TEXT("schema 7 migrates"), ClassifySaveVersion(7, SOL::Settings::SAVE_VERSION)
        == ESaveVersionAction::Migrate);
    TestTrue(TEXT("current schema is used"), ClassifySaveVersion(SOL::Settings::SAVE_VERSION,
        SOL::Settings::SAVE_VERSION) == ESaveVersionAction::Use);

    const USOLSettingsSave* fresh = NewObject<USOLSettingsSave>();
    TestTrue(TEXT("new save shows orbit lines"), fresh->bShowOrbitLines);
    TestTrue(TEXT("new save reads shown"), fresh->GetValues().bShowOrbitLines);

    // Hidden round-trips as hidden
    USOLSettingsSave* save = NewObject<USOLSettingsSave>();
    SOLSettingsModel::FSettingsValues values;
    values.bShowOrbitLines = false;
    save->SetValues(values);
    TestFalse(TEXT("SetValues stores hidden"), save->bShowOrbitLines);
    TestFalse(TEXT("GetValues gives hidden back"), save->GetValues().bShowOrbitLines);
    TestTrue(TEXT("hidden round trip"), SOLSettingsModel::AreEqual(save->GetValues(), values));

    // And shown again as shown
    values.bShowOrbitLines = true;
    save->SetValues(values);
    TestTrue(TEXT("SetValues stores shown"), save->bShowOrbitLines);
    TestTrue(TEXT("GetValues gives shown back"), save->GetValues().bShowOrbitLines);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesMigrateV1Test, "SOLTest.SettingsRules.MigrateV1",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A schema 1 save migrates to the current version with orbit lines shown and every other field kept
bool FSOLSettingsRulesMigrateV1Test::RunTest(const FString& /*parameters*/)
{
    // A schema 1 save with non-default values everywhere (and a stale orbit-lines value schema 1 never wrote)
    USOLSettingsSave* save = NewObject<USOLSettingsSave>();
    save->Version = 1;
    save->MasterVolume = 0.3f;
    save->SfxVolume = 0.4f;
    save->MusicVolume = 0.5f;
    save->MouseSensitivity = 2.5f;
    save->bInvertY = true;
    save->HudScale = 1.5f;
    save->bHudVisible = false;
    save->bDefaultFlightAssist = false;
    save->bVsync = false;
    save->QualityPreset = 1;
    save->bVsyncChosen = true;
    save->bQualityChosen = true;
    save->KeyOverrides.Add(TEXT("Fire"), TEXT("F"));
    save->KeyOverrides.Add(TEXT("ToggleMap"), TEXT("Comma"));
    save->bShowOrbitLines = false;
    save->HudEffectsStrength = 0.3f;

    save->MigrateToCurrentVersion();
    TestEqual(TEXT("upgraded to the current version"), save->Version, SOL::Settings::SAVE_VERSION);
    TestTrue(TEXT("orbit lines default to shown"), save->bShowOrbitLines);
    TestEqual(TEXT("HUD effects default from the 4 -> 5 step"), save->HudEffectsStrength,
        SOL::Settings::HUD_EFFECTS_DEFAULT, SETTINGSRULES_TEST_TOLERANCE);

    // Every other field is untouched
    TestEqual(TEXT("master kept"), save->MasterVolume, 0.3f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("sfx kept"), save->SfxVolume, 0.4f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("music kept"), save->MusicVolume, 0.5f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("sensitivity kept"), save->MouseSensitivity, 2.5f, SETTINGSRULES_TEST_TOLERANCE);
    TestTrue(TEXT("invert Y kept"), save->bInvertY);
    TestEqual(TEXT("HUD scale kept"), save->HudScale, 1.5f, SETTINGSRULES_TEST_TOLERANCE);
    TestFalse(TEXT("HUD hidden kept"), save->bHudVisible);
    TestFalse(TEXT("flight assist kept"), save->bDefaultFlightAssist);
    TestFalse(TEXT("vsync kept"), save->bVsync);
    TestEqual(TEXT("quality kept"), save->QualityPreset, 1);
    TestTrue(TEXT("vsync chosen kept"), save->bVsyncChosen);
    TestTrue(TEXT("quality chosen kept"), save->bQualityChosen);
    TestEqual(TEXT("key overrides count kept"), save->KeyOverrides.Num(), 2);
    const FString* fireKey = save->KeyOverrides.Find(TEXT("Fire"));
    TestTrue(TEXT("fire override kept"), fireKey != nullptr && *fireKey == TEXT("F"));
    TestNull(TEXT("map toggle override removed by the 3 -> 4 step"), save->KeyOverrides.Find(TEXT("ToggleMap")));
    const FString* mapKey = save->KeyOverrides.Find(TEXT("OpenMapJump"));
    TestTrue(TEXT("map override moved to the jump-map opener"), mapKey != nullptr && *mapKey == TEXT("Comma"));

    // Migrating again is a no-op
    save->MigrateToCurrentVersion();
    TestEqual(TEXT("second migrate keeps the version"), save->Version, SOL::Settings::SAVE_VERSION);
    TestTrue(TEXT("second migrate keeps the flag"), save->bShowOrbitLines);
    TestEqual(TEXT("second migrate keeps the overrides"), save->KeyOverrides.Num(), 2);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesMigrateV3RouteKeysTest, "SOLTest.SettingsRules.MigrateV3RouteKeys",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Schema 3 -> 4 (issue #20): a ToggleMap override moves to OpenMapJump unless one exists already (which wins); every
// other override and field is kept verbatim, and a second migrate changes nothing
bool FSOLSettingsRulesMigrateV3RouteKeysTest::RunTest(const FString& /*parameters*/)
{
    // A v3 save with a map toggle override and two unrelated ones
    USOLSettingsSave* save = NewObject<USOLSettingsSave>();
    save->Version = 3;
    save->bShowOrbitLines = false;
    save->MouseSensitivity = 1.75f;
    save->KeyOverrides.Add(TEXT("ToggleMap"), TEXT("Comma"));
    save->KeyOverrides.Add(TEXT("Fire"), TEXT("K"));
    save->KeyOverrides.Add(TEXT("SurfaceLock"), TEXT("Home"));
    save->MigrateToCurrentVersion();
    TestEqual(TEXT("v3 -> current"), save->Version, SOL::Settings::SAVE_VERSION);
    TestNull(TEXT("ToggleMap removed"), save->KeyOverrides.Find(TEXT("ToggleMap")));
    const FString* jumpKey = save->KeyOverrides.Find(TEXT("OpenMapJump"));
    TestTrue(TEXT("its key moved to OpenMapJump"), jumpKey != nullptr && *jumpKey == TEXT("Comma"));
    const FString* fireKey = save->KeyOverrides.Find(TEXT("Fire"));
    TestTrue(TEXT("fire kept verbatim"), fireKey != nullptr && *fireKey == TEXT("K"));
    const FString* lockKey = save->KeyOverrides.Find(TEXT("SurfaceLock"));
    TestTrue(TEXT("surface lock kept verbatim"), lockKey != nullptr && *lockKey == TEXT("Home"));
    TestEqual(TEXT("override count"), save->KeyOverrides.Num(), 3);
    TestFalse(TEXT("v3 hidden orbit lines kept"), save->bShowOrbitLines);
    TestEqual(TEXT("v3 sensitivity kept"), save->MouseSensitivity, 1.75f, SETTINGSRULES_TEST_TOLERANCE);

    // Idempotent
    const TMap<FString, FString> afterFirst = save->KeyOverrides;
    save->MigrateToCurrentVersion();
    TestEqual(TEXT("second migrate keeps the version"), save->Version, SOL::Settings::SAVE_VERSION);
    TestEqual(TEXT("second migrate keeps the count"), save->KeyOverrides.Num(), afterFirst.Num());
    const FString* jumpAgain = save->KeyOverrides.Find(TEXT("OpenMapJump"));
    TestTrue(TEXT("second migrate keeps OpenMapJump"), jumpAgain != nullptr && *jumpAgain == TEXT("Comma"));

    // An existing OpenMapJump override wins over the moved ToggleMap one
    USOLSettingsSave* both = NewObject<USOLSettingsSave>();
    both->Version = 3;
    both->KeyOverrides.Add(TEXT("ToggleMap"), TEXT("Comma"));
    both->KeyOverrides.Add(TEXT("OpenMapJump"), TEXT("Period"));
    both->MigrateToCurrentVersion();
    TestEqual(TEXT("both: v3 -> current"), both->Version, SOL::Settings::SAVE_VERSION);
    TestNull(TEXT("both: ToggleMap removed"), both->KeyOverrides.Find(TEXT("ToggleMap")));
    const FString* keptJump = both->KeyOverrides.Find(TEXT("OpenMapJump"));
    TestTrue(TEXT("both: existing OpenMapJump wins"), keptJump != nullptr && *keptJump == TEXT("Period"));
    TestEqual(TEXT("both: one override left"), both->KeyOverrides.Num(), 1);

    // A v3 save with no ToggleMap override keeps its overrides as they are
    USOLSettingsSave* plain = NewObject<USOLSettingsSave>();
    plain->Version = 3;
    plain->KeyOverrides.Add(TEXT("Boost"), TEXT("RightShift"));
    plain->MigrateToCurrentVersion();
    TestEqual(TEXT("plain: v3 -> current"), plain->Version, SOL::Settings::SAVE_VERSION);
    TestEqual(TEXT("plain: one override"), plain->KeyOverrides.Num(), 1);
    const FString* boostKey = plain->KeyOverrides.Find(TEXT("Boost"));
    TestTrue(TEXT("plain: boost kept"), boostKey != nullptr && *boostKey == TEXT("RightShift"));
    TestNull(TEXT("plain: no OpenMapJump invented"), plain->KeyOverrides.Find(TEXT("OpenMapJump")));

    // A current save holding a stray ToggleMap entry is not migrated, so it is left untouched
    USOLSettingsSave* current = NewObject<USOLSettingsSave>();
    current->Version = SOL::Settings::SAVE_VERSION;
    current->KeyOverrides.Add(TEXT("ToggleMap"), TEXT("Comma"));
    current->MigrateToCurrentVersion();
    TestNotNull(TEXT("current: untouched"), current->KeyOverrides.Find(TEXT("ToggleMap")));
    TestNull(TEXT("current: nothing moved"), current->KeyOverrides.Find(TEXT("OpenMapJump")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesMigrateNoOpTest, "SOLTest.SettingsRules.MigrateNoOp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A current, unversioned, negative or newer save is left completely unchanged by MigrateToCurrentVersion
bool FSOLSettingsRulesMigrateNoOpTest::RunTest(const FString& /*parameters*/)
{
    // A current save keeps hidden orbit lines and everything else
    USOLSettingsSave* current = NewObject<USOLSettingsSave>();
    current->Version = SOL::Settings::SAVE_VERSION;
    current->bShowOrbitLines = false;
    current->MasterVolume = 0.2f;
    current->bVsyncChosen = true;
    current->KeyOverrides.Add(TEXT("Fire"), TEXT("F"));
    current->MigrateToCurrentVersion();
    TestEqual(TEXT("current: version kept"), current->Version, SOL::Settings::SAVE_VERSION);
    TestFalse(TEXT("current: hidden stays hidden"), current->bShowOrbitLines);
    TestEqual(TEXT("current: master kept"), current->MasterVolume, 0.2f, SETTINGSRULES_TEST_TOLERANCE);
    TestTrue(TEXT("current: vsync chosen kept"), current->bVsyncChosen);
    TestEqual(TEXT("current: overrides kept"), current->KeyOverrides.Num(), 1);

    // Versions that are never migrated stay exactly as they were
    const int32 untouchedVersions[] = { 0, -1, SOL::Settings::SAVE_VERSION + 1, SOL::Settings::SAVE_VERSION + 7 };
    for (const int32 version : untouchedVersions)
    {
        USOLSettingsSave* save = NewObject<USOLSettingsSave>();
        save->Version = version;
        save->bShowOrbitLines = false;
        save->MusicVolume = 0.1f;
        save->QualityPreset = 0;
        save->MigrateToCurrentVersion();
        const FString label = FString::Printf(TEXT("version %d"), version);
        TestEqual(label + TEXT(": version kept"), save->Version, version);
        TestFalse(label + TEXT(": flag kept"), save->bShowOrbitLines);
        TestEqual(label + TEXT(": music kept"), save->MusicVolume, 0.1f, SETTINGSRULES_TEST_TOLERANCE);
        TestEqual(label + TEXT(": quality kept"), save->QualityPreset, 0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesSaveOwedOrbitLinesTest, "SOLTest.SettingsRules.SaveOwedOrbitLines",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A write is owed after an Apply, a migration or a failed write, or while the O key left the flag off its written value
bool FSOLSettingsRulesSaveOwedOrbitLinesTest::RunTest(const FString& /*parameters*/)
{
    using SOLSettingsRules::IsSaveOwed;

    // The truth table: owed || applied != written
    TestFalse(TEXT("nothing owed, same value (shown)"), IsSaveOwed(false, true, true));
    TestFalse(TEXT("nothing owed, same value (hidden)"), IsSaveOwed(false, false, false));
    TestTrue(TEXT("flag differs (hidden vs shown)"), IsSaveOwed(false, false, true));
    TestTrue(TEXT("flag differs (shown vs hidden)"), IsSaveOwed(false, true, false));
    TestTrue(TEXT("owed, same value"), IsSaveOwed(true, true, true));
    TestTrue(TEXT("owed, flag differs"), IsSaveOwed(true, false, true));

    // The subsystem's bookkeeping: loaded with lines shown, nothing owed
    bool bWritten = true;
    bool bApplied = true;
    bool bOwed = false;
    TestFalse(TEXT("after load: not owed"), IsSaveOwed(bOwed, bApplied, bWritten));

    // O twice returns to the written value: nothing to write
    bApplied = false;
    TestTrue(TEXT("after one O: owed"), IsSaveOwed(bOwed, bApplied, bWritten));
    bApplied = true;
    TestFalse(TEXT("after O twice: not owed"), IsSaveOwed(bOwed, bApplied, bWritten));

    // O once, then a successful write records the new value and clears the debt
    bApplied = false;
    TestTrue(TEXT("O once: owed"), IsSaveOwed(bOwed, bApplied, bWritten));
    bWritten = bApplied;
    bOwed = false;
    TestFalse(TEXT("after the write: not owed"), IsSaveOwed(bOwed, bApplied, bWritten));

    // A migration owes one write back even with the flag unchanged
    bOwed = true;
    TestTrue(TEXT("migration: owed"), IsSaveOwed(bOwed, bApplied, bWritten));
    bWritten = bApplied;
    bOwed = false;
    TestFalse(TEXT("after the migration write: not owed"), IsSaveOwed(bOwed, bApplied, bWritten));

    // A failed write keeps the debt even though applied now equals written
    bApplied = true;
    bWritten = bApplied;
    bOwed = true;
    TestTrue(TEXT("failed write: owed with applied == written"), IsSaveOwed(bOwed, bApplied, bWritten));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesSchemaOneRoundTripTest, "SOLTest.SettingsRules.SchemaOneRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A schema 1 save written to disk loads back as schema 1 and migrates to the current schema with orbit lines shown
bool FSOLSettingsRulesSchemaOneRoundTripTest::RunTest(const FString& /*parameters*/)
{
    const FString slotName = TEXT("SOLTest_OrbitLinesSchema1");
    constexpr int32 userIndex = 0;

    // Start clean, and always remove the test slot when the test ends, whichever path it takes
    UGameplayStatics::DeleteGameInSlot(slotName, userIndex);
    ON_SCOPE_EXIT
    {
        UGameplayStatics::DeleteGameInSlot(slotName, userIndex);
    };

    // A schema 1 file with hidden lines (a value schema 1 never wrote) and a few non-default fields
    USOLSettingsSave* written = NewObject<USOLSettingsSave>();
    written->Version = 1;
    written->bShowOrbitLines = false;
    written->MasterVolume = 0.35f;
    written->bInvertY = true;
    written->QualityPreset = 1;
    written->KeyOverrides.Add(TEXT("Fire"), TEXT("F"));
    if (!TestTrue(TEXT("schema 1 save written"), UGameplayStatics::SaveGameToSlot(written, slotName, userIndex)))
    {
        return false;
    }

    // It loads back as a schema 1 settings save
    USOLSettingsSave* loaded = Cast<USOLSettingsSave>(UGameplayStatics::LoadGameFromSlot(slotName, userIndex));
    if (!TestNotNull(TEXT("schema 1 save loaded as a settings save"), loaded))
    {
        return false;
    }
    TestEqual(TEXT("loaded as schema 1"), loaded->Version, 1);

    // Migration upgrades it, shows the lines and keeps the other fields
    loaded->MigrateToCurrentVersion();
    TestEqual(TEXT("migrated to the current schema"), loaded->Version, SOL::Settings::SAVE_VERSION);
    TestTrue(TEXT("orbit lines shown after migration"), loaded->bShowOrbitLines);
    TestEqual(TEXT("master volume kept"), loaded->MasterVolume, 0.35f, SETTINGSRULES_TEST_TOLERANCE);
    TestTrue(TEXT("invert Y kept"), loaded->bInvertY);
    TestEqual(TEXT("quality kept"), loaded->QualityPreset, 1);
    const FString* fireKey = loaded->KeyOverrides.Find(TEXT("Fire"));
    TestTrue(TEXT("key override kept"), fireKey != nullptr && *fireKey == TEXT("F"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesMigrateV4HudEffectsTest, "SOLTest.SettingsRules.MigrateV4HudEffects",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Schema 4 -> 5 (issue #28): the HUD effects strength (absent from a v4 file) becomes the default; every other field and
// override is kept, and a second migrate changes nothing
bool FSOLSettingsRulesMigrateV4HudEffectsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsRules;
    TestTrue(TEXT("a v4 save is migrated"), ClassifySaveVersion(4, SOL::Settings::SAVE_VERSION)
        == ESaveVersionAction::Migrate);
    TestTrue(TEXT("classify(4, 5) migrates"), ClassifySaveVersion(4, 5) == ESaveVersionAction::Migrate);
    TestTrue(TEXT("classify(5, 5) uses"), ClassifySaveVersion(5, 5) == ESaveVersionAction::Use);

    // A v4 save with non-default values everywhere and a stray strength a v4 file never wrote
    USOLSettingsSave* save = NewObject<USOLSettingsSave>();
    save->Version = 4;
    save->MasterVolume = 0.3f;
    save->SfxVolume = 0.4f;
    save->MusicVolume = 0.5f;
    save->MouseSensitivity = 2.5f;
    save->bInvertY = true;
    save->HudScale = 1.5f;
    save->bHudVisible = false;
    save->bDefaultFlightAssist = false;
    save->bVsync = false;
    save->QualityPreset = 1;
    save->bVsyncChosen = true;
    save->bQualityChosen = true;
    save->bShowOrbitLines = false;
    save->HudEffectsStrength = 0.3f;
    save->KeyOverrides.Add(TEXT("Fire"), TEXT("F"));
    save->KeyOverrides.Add(TEXT("ToggleMap"), TEXT("Comma"));     // Already past 3 -> 4: must not be moved again

    save->MigrateToCurrentVersion();
    TestEqual(TEXT("v4 -> current"), save->Version, SOL::Settings::SAVE_VERSION);
    TestEqual(TEXT("strength set to the default"), save->HudEffectsStrength, SOL::Settings::HUD_EFFECTS_DEFAULT,
        SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("default is 1"), save->HudEffectsStrength, 1.0f, SETTINGSRULES_TEST_TOLERANCE);

    // Every other field is untouched
    TestEqual(TEXT("master kept"), save->MasterVolume, 0.3f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("sfx kept"), save->SfxVolume, 0.4f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("music kept"), save->MusicVolume, 0.5f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("sensitivity kept"), save->MouseSensitivity, 2.5f, SETTINGSRULES_TEST_TOLERANCE);
    TestTrue(TEXT("invert Y kept"), save->bInvertY);
    TestEqual(TEXT("HUD scale kept"), save->HudScale, 1.5f, SETTINGSRULES_TEST_TOLERANCE);
    TestFalse(TEXT("HUD hidden kept"), save->bHudVisible);
    TestFalse(TEXT("flight assist kept"), save->bDefaultFlightAssist);
    TestFalse(TEXT("vsync kept"), save->bVsync);
    TestEqual(TEXT("quality kept"), save->QualityPreset, 1);
    TestTrue(TEXT("vsync chosen kept"), save->bVsyncChosen);
    TestTrue(TEXT("quality chosen kept"), save->bQualityChosen);
    TestFalse(TEXT("hidden orbit lines kept"), save->bShowOrbitLines);
    TestEqual(TEXT("override count kept"), save->KeyOverrides.Num(), 2);
    const FString* fireKey = save->KeyOverrides.Find(TEXT("Fire"));
    TestTrue(TEXT("fire override kept"), fireKey != nullptr && *fireKey == TEXT("F"));
    const FString* toggleKey = save->KeyOverrides.Find(TEXT("ToggleMap"));
    TestTrue(TEXT("v4 overrides are not re-run through 3 -> 4"), toggleKey != nullptr && *toggleKey == TEXT("Comma"));
    TestNull(TEXT("no OpenMapJump invented"), save->KeyOverrides.Find(TEXT("OpenMapJump")));

    // Idempotent: a later strength change survives a second migrate
    save->HudEffectsStrength = 0.6f;
    save->MigrateToCurrentVersion();
    TestEqual(TEXT("second migrate keeps the version"), save->Version, SOL::Settings::SAVE_VERSION);
    TestEqual(TEXT("second migrate keeps the strength"), save->HudEffectsStrength, 0.6f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("second migrate keeps the overrides"), save->KeyOverrides.Num(), 2);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesMigrateV4CurrentKeepsTest,
    "SOLTest.SettingsRules.MigrateV4CurrentKeepsHudEffects",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A current save keeps its HUD effects strength and version; the save object defaults and round-trips the strength
bool FSOLSettingsRulesMigrateV4CurrentKeepsTest::RunTest(const FString& /*parameters*/)
{
    // A current save is not migrated
    USOLSettingsSave* current = NewObject<USOLSettingsSave>();
    current->Version = SOL::Settings::SAVE_VERSION;
    current->HudEffectsStrength = 0.3f;
    current->MigrateToCurrentVersion();
    TestEqual(TEXT("current: version kept"), current->Version, SOL::Settings::SAVE_VERSION);
    TestEqual(TEXT("current: strength kept"), current->HudEffectsStrength, 0.3f, SETTINGSRULES_TEST_TOLERANCE);
    current->MigrateToCurrentVersion();
    TestEqual(TEXT("current: strength kept twice"), current->HudEffectsStrength, 0.3f, SETTINGSRULES_TEST_TOLERANCE);

    // A new save holds the default strength
    const USOLSettingsSave* fresh = NewObject<USOLSettingsSave>();
    TestEqual(TEXT("new save: default strength"), fresh->HudEffectsStrength, SOL::Settings::HUD_EFFECTS_DEFAULT,
        SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("new save: GetValues default strength"), fresh->GetValues().HudEffectsStrength,
        SOL::Settings::HUD_EFFECTS_DEFAULT, SETTINGSRULES_TEST_TOLERANCE);

    // SetValues / GetValues round-trip the strength
    USOLSettingsSave* save = NewObject<USOLSettingsSave>();
    SOLSettingsModel::FSettingsValues values;
    values.HudEffectsStrength = 1.65f;
    save->SetValues(values);
    TestEqual(TEXT("SetValues stores the strength"), save->HudEffectsStrength, 1.65f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("GetValues gives it back"), save->GetValues().HudEffectsStrength, 1.65f,
        SETTINGSRULES_TEST_TOLERANCE);
    TestTrue(TEXT("round trip"), SOLSettingsModel::AreEqual(save->GetValues(), values));
    values.HudEffectsStrength = SOL::Settings::HUD_EFFECTS_MIN;
    save->SetValues(values);
    TestEqual(TEXT("off round-trips"), save->GetValues().HudEffectsStrength, SOL::Settings::HUD_EFFECTS_MIN,
        SETTINGSRULES_TEST_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesMouseSteerSaveTest, "SOLTest.SettingsRules.MouseSteerAtStartSave",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Schema 6 carries the steer-at-start flag: a new save holds off, and SetValues/GetValues round-trip both values
bool FSOLSettingsRulesMouseSteerSaveTest::RunTest(const FString& /*parameters*/)
{
    const USOLSettingsSave* fresh = NewObject<USOLSettingsSave>();
    TestFalse(TEXT("new save: steer at start off"), fresh->bMouseSteerAtStart);
    TestFalse(TEXT("new save: GetValues off"), fresh->GetValues().bMouseSteerAtStart);

    // On round-trips as on
    USOLSettingsSave* save = NewObject<USOLSettingsSave>();
    SOLSettingsModel::FSettingsValues values;
    values.bMouseSteerAtStart = true;
    save->SetValues(values);
    TestTrue(TEXT("SetValues stores on"), save->bMouseSteerAtStart);
    TestTrue(TEXT("GetValues gives on back"), save->GetValues().bMouseSteerAtStart);
    TestTrue(TEXT("on round trip"), SOLSettingsModel::AreEqual(save->GetValues(), values));

    // And off again as off
    values.bMouseSteerAtStart = false;
    save->SetValues(values);
    TestFalse(TEXT("SetValues stores off"), save->bMouseSteerAtStart);
    TestFalse(TEXT("GetValues gives off back"), save->GetValues().bMouseSteerAtStart);
    TestTrue(TEXT("off round trip"), SOLSettingsModel::AreEqual(save->GetValues(), values));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesMigrateV5MouseSteerTest, "SOLTest.SettingsRules.MigrateV5MouseSteer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Schema 5 -> 6 (issue #31): the steer-at-start flag (absent from older files) becomes off; everything else is kept
bool FSOLSettingsRulesMigrateV5MouseSteerTest::RunTest(const FString& /*parameters*/)
{
    // A v5 save with a stale steer flag a v5 file never wrote, and non-default values elsewhere
    USOLSettingsSave* save = NewObject<USOLSettingsSave>();
    save->Version = 5;
    save->bMouseSteerAtStart = true;
    save->HudEffectsStrength = 0.3f;
    save->bShowOrbitLines = false;
    save->bDefaultFlightAssist = false;
    save->MasterVolume = 0.45f;
    save->KeyOverrides.Add(TEXT("Fire"), TEXT("F"));
    save->MigrateToCurrentVersion();
    TestEqual(TEXT("v5 -> 8 (through 5 -> 6, 6 -> 7 and 7 -> 8)"), save->Version, 8);
    TestEqual(TEXT("v5 -> current"), save->Version, SOL::Settings::SAVE_VERSION);
    TestFalse(TEXT("steer at start set to off"), save->bMouseSteerAtStart);
    TestEqual(TEXT("strength kept"), save->HudEffectsStrength, 0.3f, SETTINGSRULES_TEST_TOLERANCE);
    TestFalse(TEXT("hidden orbit lines kept"), save->bShowOrbitLines);
    TestFalse(TEXT("flight assist kept"), save->bDefaultFlightAssist);
    TestEqual(TEXT("master kept"), save->MasterVolume, 0.45f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("override count kept"), save->KeyOverrides.Num(), 1);
    const FString* fireKey = save->KeyOverrides.Find(TEXT("Fire"));
    TestTrue(TEXT("fire override kept"), fireKey != nullptr && *fireKey == TEXT("F"));

    // Older saves also end at the current schema with the flag off
    const int32 olderVersions[] = { 1, 4 };
    for (const int32 version : olderVersions)
    {
        USOLSettingsSave* older = NewObject<USOLSettingsSave>();
        older->Version = version;
        older->bMouseSteerAtStart = true;
        older->MigrateToCurrentVersion();
        const FString label = FString::Printf(TEXT("v%d"), version);
        TestEqual(label + TEXT(": ends at the current schema"), older->Version, SOL::Settings::SAVE_VERSION);
        TestFalse(label + TEXT(": steer at start off"), older->bMouseSteerAtStart);
    }

    // A v6 save (which has the flag) keeps the player's choice through 6 -> 7 -> 8, and a current one across two calls
    USOLSettingsSave* v6 = NewObject<USOLSettingsSave>();
    v6->Version = 6;
    v6->bMouseSteerAtStart = true;
    v6->MigrateToCurrentVersion();
    TestEqual(TEXT("v6: migrated to the current schema"), v6->Version, SOL::Settings::SAVE_VERSION);
    TestTrue(TEXT("v6: steer at start kept"), v6->bMouseSteerAtStart);
    USOLSettingsSave* current = NewObject<USOLSettingsSave>();
    current->Version = SOL::Settings::SAVE_VERSION;
    current->bMouseSteerAtStart = true;
    current->MigrateToCurrentVersion();
    TestEqual(TEXT("current: version kept"), current->Version, SOL::Settings::SAVE_VERSION);
    TestTrue(TEXT("current: steer at start kept"), current->bMouseSteerAtStart);
    current->MigrateToCurrentVersion();
    TestTrue(TEXT("current: steer at start kept twice"), current->bMouseSteerAtStart);

    // Unversioned and newer saves are never migrated
    const int32 untouchedVersions[] = { 0, SOL::Settings::SAVE_VERSION + 1 };
    for (const int32 version : untouchedVersions)
    {
        USOLSettingsSave* untouched = NewObject<USOLSettingsSave>();
        untouched->Version = version;
        untouched->bMouseSteerAtStart = true;
        untouched->MigrateToCurrentVersion();
        const FString label = FString::Printf(TEXT("version %d"), version);
        TestEqual(label + TEXT(": version kept"), untouched->Version, version);
        TestTrue(label + TEXT(": steer at start kept"), untouched->bMouseSteerAtStart);
    }
    return true;
}

// Issue #22 overlay profiles in the settings save (schema 7), written against the issue #22 contract (SDD 22)
// before the production code

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns whether two saved overlay maps hold exactly the same ids and values
    bool SettingsRulesSameOverlayMap(const TMap<FString, bool>& a, const TMap<FString, bool>& b)
    {
        if (a.Num() != b.Num())
        {
            return false;
        }
        for (const TPair<FString, bool>& entry : a)
        {
            const bool* other = b.Find(entry.Key);
            if (other == nullptr || *other != entry.Value)
            {
                return false;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns one saved overlay profile holding a mask
    FSOLOverlayProfileSave SettingsRulesOverlayProfile(const uint32 mask)
    {
        FSOLOverlayProfileSave profile;
        profile.Overlays = SOLOverlay::ToSavedProfile(mask);
        return profile;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesMigrateV6OverlaysTest, "SOLTest.SettingsRules.MigrateV6Overlays",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Schema 6 -> 7 (issue #22): one default overlay profile, active 0; key overrides and every other field are kept
bool FSOLSettingsRulesMigrateV6OverlaysTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsRules;
    TestTrue(TEXT("a v6 save is migrated"), ClassifySaveVersion(6, SOL::Settings::SAVE_VERSION)
        == ESaveVersionAction::Migrate);

    // A v6 save with stale overlay fields a v6 file never wrote and non-default values elsewhere
    USOLSettingsSave* save = NewObject<USOLSettingsSave>();
    save->Version = 6;
    save->OverlayProfiles.Add(SettingsRulesOverlayProfile(0u));
    save->OverlayProfiles.Add(SettingsRulesOverlayProfile(0u));
    save->ActiveOverlayProfile = 1;
    save->bMouseSteerAtStart = true;
    save->HudEffectsStrength = 0.3f;
    save->bShowOrbitLines = false;
    save->MasterVolume = 0.45f;
    save->QualityPreset = 2;
    save->KeyOverrides.Add(TEXT("ToggleOrbitLines"), TEXT("K"));
    save->KeyOverrides.Add(TEXT("Fire"), TEXT("F"));
    save->MigrateToCurrentVersion();

    TestEqual(TEXT("v6 -> 8 (through 6 -> 7 and 7 -> 8)"), save->Version, 8);
    TestEqual(TEXT("v6 -> current"), save->Version, SOL::Settings::SAVE_VERSION);
    TestEqual(TEXT("one overlay profile"), save->OverlayProfiles.Num(), 1);
    if (save->OverlayProfiles.Num() == 1)
    {
        TestTrue(TEXT("the profile holds the defaults"), SettingsRulesSameOverlayMap(save->OverlayProfiles[0].Overlays,
            SOLOverlay::ToSavedProfile(SOLOverlay::DefaultMask())));
    }
    TestEqual(TEXT("active profile 0"), save->ActiveOverlayProfile, 0);

    // Everything else is untouched, the orbit-lines override verbatim
    TestTrue(TEXT("steer at start kept"), save->bMouseSteerAtStart);
    TestEqual(TEXT("strength kept"), save->HudEffectsStrength, 0.3f, SETTINGSRULES_TEST_TOLERANCE);
    TestFalse(TEXT("hidden orbit lines kept"), save->bShowOrbitLines);
    TestEqual(TEXT("master kept"), save->MasterVolume, 0.45f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("quality kept"), save->QualityPreset, 2);
    TestEqual(TEXT("override count kept"), save->KeyOverrides.Num(), 2);
    const FString* orbitKey = save->KeyOverrides.Find(TEXT("ToggleOrbitLines"));
    TestTrue(TEXT("orbit-lines override kept verbatim"), orbitKey != nullptr && *orbitKey == TEXT("K"));
    const FString* fireKey = save->KeyOverrides.Find(TEXT("Fire"));
    TestTrue(TEXT("fire override kept"), fireKey != nullptr && *fireKey == TEXT("F"));

    // The migrated save reads as the default overlay prefs
    const SOLOverlay::FOverlayPrefs prefs = save->GetValues().OverlayPrefs;
    TestTrue(TEXT("reads as the default prefs"), SOLOverlay::AreEqual(prefs, SOLOverlay::MakeDefaultPrefs()));

    // Idempotent: a later overlay change survives a second migrate
    save->OverlayProfiles[0] = SettingsRulesOverlayProfile(0x3u);
    save->MigrateToCurrentVersion();
    TestEqual(TEXT("second migrate keeps the version"), save->Version, SOL::Settings::SAVE_VERSION);
    TestTrue(TEXT("second migrate keeps the profile"), SettingsRulesSameOverlayMap(save->OverlayProfiles[0].Overlays,
        SOLOverlay::ToSavedProfile(0x3u)));

    // A schema 1 save migrates all the way with one default profile
    USOLSettingsSave* v1 = NewObject<USOLSettingsSave>();
    v1->Version = 1;
    v1->MigrateToCurrentVersion();
    TestEqual(TEXT("v1 -> current"), v1->Version, SOL::Settings::SAVE_VERSION);
    TestEqual(TEXT("v1: one overlay profile"), v1->OverlayProfiles.Num(), 1);
    if (v1->OverlayProfiles.Num() == 1)
    {
        TestTrue(TEXT("v1: default profile"), SettingsRulesSameOverlayMap(v1->OverlayProfiles[0].Overlays,
            SOLOverlay::ToSavedProfile(SOLOverlay::DefaultMask())));
    }
    TestEqual(TEXT("v1: active profile 0"), v1->ActiveOverlayProfile, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesCurrentOverlaysTest, "SOLTest.SettingsRules.CurrentKeepsOverlays",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A current (schema 8) save is not migrated: its overlay profiles and active profile stay exactly as they were
bool FSOLSettingsRulesCurrentOverlaysTest::RunTest(const FString& /*parameters*/)
{
    USOLSettingsSave* current = NewObject<USOLSettingsSave>();
    current->Version = SOL::Settings::SAVE_VERSION;
    current->OverlayProfiles.Add(SettingsRulesOverlayProfile(0x1u));
    current->OverlayProfiles.Add(SettingsRulesOverlayProfile(SOLOverlay::AllBitsMask()));
    current->ActiveOverlayProfile = 1;
    current->KeyOverrides.Add(TEXT("ToggleOrbitLines"), TEXT("K"));
    current->MigrateToCurrentVersion();
    TestEqual(TEXT("version kept"), current->Version, SOL::Settings::SAVE_VERSION);
    TestEqual(TEXT("two profiles kept"), current->OverlayProfiles.Num(), 2);
    if (current->OverlayProfiles.Num() == 2)
    {
        TestTrue(TEXT("profile 0 kept"), SettingsRulesSameOverlayMap(current->OverlayProfiles[0].Overlays,
            SOLOverlay::ToSavedProfile(0x1u)));
        TestTrue(TEXT("profile 1 kept"), SettingsRulesSameOverlayMap(current->OverlayProfiles[1].Overlays,
            SOLOverlay::ToSavedProfile(SOLOverlay::AllBitsMask())));
    }
    TestEqual(TEXT("active profile kept"), current->ActiveOverlayProfile, 1);
    const FString* orbitKey = current->KeyOverrides.Find(TEXT("ToggleOrbitLines"));
    TestTrue(TEXT("override kept"), orbitKey != nullptr && *orbitKey == TEXT("K"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesOverlaySaveValuesTest, "SOLTest.SettingsRules.OverlaySaveValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// GetValues reads the saved profiles (clamped to MAX_PROFILES, empty = the defaults) and SetValues writes one entry
// per profile; the two round-trip
bool FSOLSettingsRulesOverlaySaveValuesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;

    // A new save reads as the default prefs
    const USOLSettingsSave* fresh = NewObject<USOLSettingsSave>();
    TestEqual(TEXT("new save: no profiles stored"), fresh->OverlayProfiles.Num(), 0);
    TestEqual(TEXT("new save: active 0"), fresh->ActiveOverlayProfile, 0);
    const FOverlayPrefs freshPrefs = fresh->GetValues().OverlayPrefs;
    TestEqual(TEXT("empty: one profile"), freshPrefs.ProfileCount, 1);
    TestEqual(TEXT("empty: active 0"), freshPrefs.ActiveProfile, 0);
    TestEqual(TEXT("empty: default mask"), freshPrefs.Masks[0], DefaultMask());

    // Three stored profiles read back as three masks with the stored active index
    USOLSettingsSave* three = NewObject<USOLSettingsSave>();
    three->OverlayProfiles.Add(SettingsRulesOverlayProfile(0x11u));
    three->OverlayProfiles.Add(SettingsRulesOverlayProfile(0x22u));
    TMap<FString, bool> partial;
    partial.Add(TEXT("asteroidorbits"), true);
    FSOLOverlayProfileSave partialProfile;
    partialProfile.Overlays = partial;
    three->OverlayProfiles.Add(partialProfile);
    three->ActiveOverlayProfile = 2;
    const FOverlayPrefs threePrefs = three->GetValues().OverlayPrefs;
    TestEqual(TEXT("three: count"), threePrefs.ProfileCount, 3);
    TestEqual(TEXT("three: active carried"), threePrefs.ActiveProfile, 2);
    TestEqual(TEXT("three: mask 0"), threePrefs.Masks[0], 0x11u);
    TestEqual(TEXT("three: mask 1"), threePrefs.Masks[1], 0x22u);
    TestEqual(TEXT("three: partial profile fills defaults"), threePrefs.Masks[2], FromSavedProfile(partial));
    TestEqual(TEXT("three: partial profile = default + asteroid orbits"), threePrefs.Masks[2], AllBitsMask());

    // More stored profiles than MAX_PROFILES are clamped
    USOLSettingsSave* many = NewObject<USOLSettingsSave>();
    for (int32 index = 0; index < SOL::Overlay::MAX_PROFILES + 2; ++index)
    {
        many->OverlayProfiles.Add(SettingsRulesOverlayProfile(static_cast<uint32>(index)));
    }
    const FOverlayPrefs manyPrefs = many->GetValues().OverlayPrefs;
    TestEqual(TEXT("many: clamped to MAX_PROFILES"), manyPrefs.ProfileCount, SOL::Overlay::MAX_PROFILES);
    TestEqual(TEXT("many: last kept mask"), manyPrefs.Masks[SOL::Overlay::MAX_PROFILES - 1],
        static_cast<uint32>(SOL::Overlay::MAX_PROFILES - 1));

    // SetValues writes one entry per profile and the active index; GetValues gives the same prefs back
    SOLSettingsModel::FSettingsValues values;
    values.OverlayPrefs = MakeDefaultPrefs();
    values.OverlayPrefs.ProfileCount = 2;
    values.OverlayPrefs.ActiveProfile = 1;
    values.OverlayPrefs.Masks[0] = DefaultMask() & ~(1u << static_cast<uint32>(EOverlay::IconShips));
    values.OverlayPrefs.Masks[1] = 0x5u;
    USOLSettingsSave* save = NewObject<USOLSettingsSave>();
    save->SetValues(values);
    TestEqual(TEXT("SetValues: two profiles"), save->OverlayProfiles.Num(), 2);
    TestEqual(TEXT("SetValues: active 1"), save->ActiveOverlayProfile, 1);
    if (save->OverlayProfiles.Num() == 2)
    {
        TestTrue(TEXT("SetValues: profile 0"), SettingsRulesSameOverlayMap(save->OverlayProfiles[0].Overlays,
            ToSavedProfile(values.OverlayPrefs.Masks[0])));
        TestTrue(TEXT("SetValues: profile 1"), SettingsRulesSameOverlayMap(save->OverlayProfiles[1].Overlays,
            ToSavedProfile(0x5u)));
    }
    const FOverlayPrefs roundTrip = save->GetValues().OverlayPrefs;
    TestTrue(TEXT("prefs round trip"), AreEqual(roundTrip, values.OverlayPrefs));
    TestTrue(TEXT("values round trip"), SOLSettingsModel::AreEqual(save->GetValues(), values));

    // Writing fewer profiles later shrinks the stored list
    values.OverlayPrefs = MakeDefaultPrefs();
    save->SetValues(values);
    TestEqual(TEXT("SetValues: back to one profile"), save->OverlayProfiles.Num(), 1);
    TestEqual(TEXT("SetValues: active 0"), save->ActiveOverlayProfile, 0);
    return true;
}

// Issue #32 Chase Down safety margin in the settings save (schema 8), written against the SDD 32 section 3.10
// contract before the production code

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesMigrateV7ChaseMarginTest, "SOLTest.SettingsRules.MigrateV7ChaseMargin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Schema 7 -> 8 (issue #32): the Chase Down safety margin (absent from a v7 file) becomes 0.25; volumes, key overrides,
// overlay profiles and every other field are kept, and a second migrate changes nothing
bool FSOLSettingsRulesMigrateV7ChaseMarginTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsRules;
    TestEqual(TEXT("schema version is 8"), SOL::Settings::SAVE_VERSION, 8);
    TestTrue(TEXT("a v7 save is migrated"), ClassifySaveVersion(7, SOL::Settings::SAVE_VERSION)
        == ESaveVersionAction::Migrate);
    TestTrue(TEXT("a v8 save is used"), ClassifySaveVersion(8, SOL::Settings::SAVE_VERSION)
        == ESaveVersionAction::Use);
    TestTrue(TEXT("a v9 (newer) save is ignored"), ClassifySaveVersion(9, SOL::Settings::SAVE_VERSION)
        == ESaveVersionAction::UseDefaults);

    // A v7 save with non-default values everywhere and a stray margin a v7 file never wrote
    USOLSettingsSave* save = NewObject<USOLSettingsSave>();
    save->Version = 7;
    save->MasterVolume = 0.3f;
    save->SfxVolume = 0.4f;
    save->MusicVolume = 0.5f;
    save->MouseSensitivity = 2.5f;
    save->bInvertY = true;
    save->HudScale = 1.5f;
    save->bHudVisible = false;
    save->bDefaultFlightAssist = false;
    save->bVsync = false;
    save->QualityPreset = 1;
    save->bVsyncChosen = true;
    save->bQualityChosen = true;
    save->bShowOrbitLines = false;
    save->HudEffectsStrength = 0.3f;
    save->bMouseSteerAtStart = true;
    save->OverlayProfiles.Add(SettingsRulesOverlayProfile(0x3u));
    save->OverlayProfiles.Add(SettingsRulesOverlayProfile(SOLOverlay::AllBitsMask()));
    save->ActiveOverlayProfile = 1;
    save->KeyOverrides.Add(TEXT("Fire"), TEXT("F"));
    save->KeyOverrides.Add(TEXT("ToggleOrbitLines"), TEXT("K"));
    save->KeyOverrides.Add(TEXT("ToggleMap"), TEXT("Comma"));     // Already past 3 -> 4: must not be moved again
    save->ChaseSafetyMargin = 0.05f;

    save->MigrateToCurrentVersion();
    TestEqual(TEXT("v7 -> 8"), save->Version, 8);
    TestEqual(TEXT("v7 -> current"), save->Version, SOL::Settings::SAVE_VERSION);
    TestEqual(TEXT("margin set to the default"), save->ChaseSafetyMargin, SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT,
        SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("default is 0.25"), save->ChaseSafetyMargin, 0.25f, SETTINGSRULES_TEST_TOLERANCE);

    // Volumes and every other scalar field are untouched
    TestEqual(TEXT("master kept"), save->MasterVolume, 0.3f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("sfx kept"), save->SfxVolume, 0.4f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("music kept"), save->MusicVolume, 0.5f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("sensitivity kept"), save->MouseSensitivity, 2.5f, SETTINGSRULES_TEST_TOLERANCE);
    TestTrue(TEXT("invert Y kept"), save->bInvertY);
    TestEqual(TEXT("HUD scale kept"), save->HudScale, 1.5f, SETTINGSRULES_TEST_TOLERANCE);
    TestFalse(TEXT("HUD hidden kept"), save->bHudVisible);
    TestFalse(TEXT("flight assist kept"), save->bDefaultFlightAssist);
    TestFalse(TEXT("vsync kept"), save->bVsync);
    TestEqual(TEXT("quality kept"), save->QualityPreset, 1);
    TestTrue(TEXT("vsync chosen kept"), save->bVsyncChosen);
    TestTrue(TEXT("quality chosen kept"), save->bQualityChosen);
    TestFalse(TEXT("hidden orbit lines kept"), save->bShowOrbitLines);
    TestEqual(TEXT("HUD effects strength kept"), save->HudEffectsStrength, 0.3f, SETTINGSRULES_TEST_TOLERANCE);
    TestTrue(TEXT("steer at start kept"), save->bMouseSteerAtStart);

    // The key overrides are kept verbatim
    TestEqual(TEXT("override count kept"), save->KeyOverrides.Num(), 3);
    const FString* fireKey = save->KeyOverrides.Find(TEXT("Fire"));
    TestTrue(TEXT("fire override kept"), fireKey != nullptr && *fireKey == TEXT("F"));
    const FString* orbitKey = save->KeyOverrides.Find(TEXT("ToggleOrbitLines"));
    TestTrue(TEXT("orbit-lines override kept"), orbitKey != nullptr && *orbitKey == TEXT("K"));
    const FString* toggleKey = save->KeyOverrides.Find(TEXT("ToggleMap"));
    TestTrue(TEXT("v7 overrides are not re-run through 3 -> 4"), toggleKey != nullptr && *toggleKey == TEXT("Comma"));
    TestNull(TEXT("no OpenMapJump invented"), save->KeyOverrides.Find(TEXT("OpenMapJump")));

    // The overlay profiles are kept (not reset by the 6 -> 7 step)
    TestEqual(TEXT("two overlay profiles kept"), save->OverlayProfiles.Num(), 2);
    if (save->OverlayProfiles.Num() == 2)
    {
        TestTrue(TEXT("profile 0 kept"), SettingsRulesSameOverlayMap(save->OverlayProfiles[0].Overlays,
            SOLOverlay::ToSavedProfile(0x3u)));
        TestTrue(TEXT("profile 1 kept"), SettingsRulesSameOverlayMap(save->OverlayProfiles[1].Overlays,
            SOLOverlay::ToSavedProfile(SOLOverlay::AllBitsMask())));
    }
    TestEqual(TEXT("active overlay profile kept"), save->ActiveOverlayProfile, 1);

    // Idempotent: a second migrate changes nothing, and a later margin change survives a third
    save->MigrateToCurrentVersion();
    TestEqual(TEXT("second migrate keeps the version"), save->Version, SOL::Settings::SAVE_VERSION);
    TestEqual(TEXT("second migrate keeps the margin"), save->ChaseSafetyMargin, 0.25f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("second migrate keeps the overrides"), save->KeyOverrides.Num(), 3);
    TestEqual(TEXT("second migrate keeps the profiles"), save->OverlayProfiles.Num(), 2);
    TestEqual(TEXT("second migrate keeps the master volume"), save->MasterVolume, 0.3f, SETTINGSRULES_TEST_TOLERANCE);
    save->ChaseSafetyMargin = 0.1f;
    save->MigrateToCurrentVersion();
    TestEqual(TEXT("a later margin change survives a migrate"), save->ChaseSafetyMargin, 0.1f,
        SETTINGSRULES_TEST_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesChaseMarginVersionsTest,
    "SOLTest.SettingsRules.ChaseMarginVersions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Saves 1 to 7 reach schema 8 with the default margin; a v8 save keeps its margin; a v9 (newer) save is left alone
bool FSOLSettingsRulesChaseMarginVersionsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsRules;

    // Every older schema migrates and ends at 8 with the default margin
    for (int32 version = 1; version <= 7; ++version)
    {
        const FString label = FString::Printf(TEXT("v%d"), version);
        TestTrue(label + TEXT(": classified as migrate"), ClassifySaveVersion(version, SOL::Settings::SAVE_VERSION)
            == ESaveVersionAction::Migrate);
        USOLSettingsSave* older = NewObject<USOLSettingsSave>();
        older->Version = version;
        older->ChaseSafetyMargin = 0.05f;
        older->MasterVolume = 0.45f;
        older->MigrateToCurrentVersion();
        TestEqual(label + TEXT(": ends at 8"), older->Version, 8);
        TestEqual(label + TEXT(": default margin"), older->ChaseSafetyMargin,
            SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT, SETTINGSRULES_TEST_TOLERANCE);
        TestEqual(label + TEXT(": master kept"), older->MasterVolume, 0.45f, SETTINGSRULES_TEST_TOLERANCE);
    }

    // A schema 1 save goes through every step to 8 and keeps its other fields
    USOLSettingsSave* v1 = NewObject<USOLSettingsSave>();
    v1->Version = 1;
    v1->MusicVolume = 0.2f;
    v1->KeyOverrides.Add(TEXT("Fire"), TEXT("F"));
    v1->MigrateToCurrentVersion();
    TestEqual(TEXT("v1 -> 8"), v1->Version, 8);
    TestEqual(TEXT("v1: margin 0.25"), v1->ChaseSafetyMargin, 0.25f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("v1: music kept"), v1->MusicVolume, 0.2f, SETTINGSRULES_TEST_TOLERANCE);
    const FString* fireKey = v1->KeyOverrides.Find(TEXT("Fire"));
    TestTrue(TEXT("v1: fire override kept"), fireKey != nullptr && *fireKey == TEXT("F"));
    TestEqual(TEXT("v1: one overlay profile"), v1->OverlayProfiles.Num(), 1);
    v1->MigrateToCurrentVersion();
    TestEqual(TEXT("v1: second migrate keeps 8"), v1->Version, 8);
    TestEqual(TEXT("v1: second migrate keeps the margin"), v1->ChaseSafetyMargin, 0.25f, SETTINGSRULES_TEST_TOLERANCE);

    // A schema 8 save is used as is: its margin is the player's choice
    USOLSettingsSave* v8 = NewObject<USOLSettingsSave>();
    v8->Version = 8;
    v8->ChaseSafetyMargin = 0.1f;
    v8->MigrateToCurrentVersion();
    TestEqual(TEXT("v8: version kept"), v8->Version, 8);
    TestEqual(TEXT("v8: margin kept"), v8->ChaseSafetyMargin, 0.1f, SETTINGSRULES_TEST_TOLERANCE);
    v8->ChaseSafetyMargin = 0.0f;
    v8->MigrateToCurrentVersion();
    TestEqual(TEXT("v8: margin 0 kept"), v8->ChaseSafetyMargin, 0.0f, SETTINGSRULES_TEST_TOLERANCE);

    // A newer (v9) or unversioned save is never migrated
    const int32 untouchedVersions[] = { 0, 9 };
    for (const int32 version : untouchedVersions)
    {
        USOLSettingsSave* untouched = NewObject<USOLSettingsSave>();
        untouched->Version = version;
        untouched->ChaseSafetyMargin = 0.1f;
        untouched->MigrateToCurrentVersion();
        const FString label = FString::Printf(TEXT("version %d"), version);
        TestEqual(label + TEXT(": version kept"), untouched->Version, version);
        TestEqual(label + TEXT(": margin kept"), untouched->ChaseSafetyMargin, 0.1f, SETTINGSRULES_TEST_TOLERANCE);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesChaseMarginSaveValuesTest,
    "SOLTest.SettingsRules.ChaseMarginSaveValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A new save holds the default margin, and SetValues / GetValues round-trip it
bool FSOLSettingsRulesChaseMarginSaveValuesTest::RunTest(const FString& /*parameters*/)
{
    // A new save holds the default margin
    const USOLSettingsSave* fresh = NewObject<USOLSettingsSave>();
    TestEqual(TEXT("new save: default margin"), fresh->ChaseSafetyMargin, SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT,
        SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("new save: GetValues default margin"), fresh->GetValues().ChaseSafetyMargin,
        SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT, SETTINGSRULES_TEST_TOLERANCE);

    // SetValues / GetValues round-trip the margin
    USOLSettingsSave* save = NewObject<USOLSettingsSave>();
    SOLSettingsModel::FSettingsValues values;
    values.ChaseSafetyMargin = 0.12f;
    save->SetValues(values);
    TestEqual(TEXT("SetValues stores the margin"), save->ChaseSafetyMargin, 0.12f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("GetValues gives it back"), save->GetValues().ChaseSafetyMargin, 0.12f,
        SETTINGSRULES_TEST_TOLERANCE);
    TestTrue(TEXT("round trip"), SOLSettingsModel::AreEqual(save->GetValues(), values));

    // Both range ends round-trip
    values.ChaseSafetyMargin = SOL::Settings::CHASE_SAFETY_MARGIN_MIN;
    save->SetValues(values);
    TestEqual(TEXT("MIN round-trips"), save->GetValues().ChaseSafetyMargin, SOL::Settings::CHASE_SAFETY_MARGIN_MIN,
        SETTINGSRULES_TEST_TOLERANCE);
    values.ChaseSafetyMargin = SOL::Settings::CHASE_SAFETY_MARGIN_MAX;
    save->SetValues(values);
    TestEqual(TEXT("MAX round-trips"), save->GetValues().ChaseSafetyMargin, SOL::Settings::CHASE_SAFETY_MARGIN_MAX,
        SETTINGSRULES_TEST_TOLERANCE);
    TestTrue(TEXT("MAX round trip"), SOLSettingsModel::AreEqual(save->GetValues(), values));
    return true;
}

#endif
