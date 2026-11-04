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
// Schema 2 to 9 carried one global orbit-lines flag. From schema 10 (SDD 34 D18) it is a legacy field read only by the
// 9 -> 10 step: GetValues takes the Alt+O switch from each profile's bOrbitLinesOn, never from the legacy field
bool FSOLSettingsRulesOrbitLinesSaveTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsRules;
    TestEqual(TEXT("schema version is 10"), SOL::Settings::SAVE_VERSION, 10);
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
    TestTrue(TEXT("schema 8 migrates"), ClassifySaveVersion(8, SOL::Settings::SAVE_VERSION)
        == ESaveVersionAction::Migrate);
    TestTrue(TEXT("schema 9 migrates"), ClassifySaveVersion(9, SOL::Settings::SAVE_VERSION)
        == ESaveVersionAction::Migrate);
    TestTrue(TEXT("current schema is used"), ClassifySaveVersion(SOL::Settings::SAVE_VERSION,
        SOL::Settings::SAVE_VERSION) == ESaveVersionAction::Use);

    // A new save keeps the legacy field at its default and reads every profile's switch on
    const USOLSettingsSave* fresh = NewObject<USOLSettingsSave>();
    TestTrue(TEXT("new save: legacy field shown"), fresh->bShowOrbitLines);
    TestTrue(TEXT("new save reads the switch on"), SOLOverlay::AreOrbitLinesOn(fresh->GetValues().OverlayPrefs));

    // A current save with the legacy field hidden but profile 1's switch on reads the switch on (legacy not read)
    USOLSettingsSave* legacyHidden = NewObject<USOLSettingsSave>();
    legacyHidden->Version = SOL::Settings::SAVE_VERSION;
    legacyHidden->bShowOrbitLines = false;
    FSOLOverlayProfileSave shownProfile;
    shownProfile.Overlays = SOLOverlay::ToSavedProfile(SOLOverlay::DefaultMask());
    shownProfile.bEnabled = true;
    shownProfile.bOrbitLinesOn = true;
    legacyHidden->OverlayProfiles.Add(shownProfile);
    TestTrue(TEXT("legacy hidden, profile shown: reads shown"),
        SOLOverlay::AreOrbitLinesOn(legacyHidden->GetValues().OverlayPrefs));

    // And the other way round: the legacy field shown, profile 1's switch off, reads off
    USOLSettingsSave* profileHidden = NewObject<USOLSettingsSave>();
    profileHidden->Version = SOL::Settings::SAVE_VERSION;
    profileHidden->bShowOrbitLines = true;
    FSOLOverlayProfileSave hiddenProfile = shownProfile;
    hiddenProfile.bOrbitLinesOn = false;
    profileHidden->OverlayProfiles.Add(hiddenProfile);
    TestFalse(TEXT("legacy shown, profile hidden: reads hidden"),
        SOLOverlay::AreOrbitLinesOn(profileHidden->GetValues().OverlayPrefs));

    // The switch round-trips through the profiles: hidden as hidden, then shown as shown
    USOLSettingsSave* save = NewObject<USOLSettingsSave>();
    SOLSettingsModel::FSettingsValues values;
    SOLOverlay::SetOrbitLinesOn(values.OverlayPrefs, false);
    save->SetValues(values);
    TestTrue(TEXT("SetValues writes profile 1"), save->OverlayProfiles.Num() > 0);
    if (save->OverlayProfiles.Num() > 0)
    {
        TestFalse(TEXT("SetValues stores hidden in profile 1"), save->OverlayProfiles[0].bOrbitLinesOn);
    }
    TestFalse(TEXT("GetValues gives hidden back"), SOLOverlay::AreOrbitLinesOn(save->GetValues().OverlayPrefs));
    TestTrue(TEXT("hidden round trip"), SOLSettingsModel::AreEqual(save->GetValues(), values));
    SOLOverlay::SetOrbitLinesOn(values.OverlayPrefs, true);
    save->SetValues(values);
    if (save->OverlayProfiles.Num() > 0)
    {
        TestTrue(TEXT("SetValues stores shown in profile 1"), save->OverlayProfiles[0].bOrbitLinesOn);
    }
    TestTrue(TEXT("GetValues gives shown back"), SOLOverlay::AreOrbitLinesOn(save->GetValues().OverlayPrefs));
    TestTrue(TEXT("shown round trip"), SOLSettingsModel::AreEqual(save->GetValues(), values));
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
    TestTrue(TEXT("space dust on from the 9 -> 10 step"), save->bSpaceDust);
    TestEqual(TEXT("one overlay profile"), save->OverlayProfiles.Num(), 1);
    if (save->OverlayProfiles.Num() == 1)
    {
        TestTrue(TEXT("profile 1 enabled"), save->OverlayProfiles[0].bEnabled);
        TestTrue(TEXT("profile 1 master on"), save->OverlayProfiles[0].bMasterOn);
        TestTrue(TEXT("profile 1 orbit lines from the shown legacy flag"), save->OverlayProfiles[0].bOrbitLinesOn);
        TestFalse(TEXT("profile 1 has no SpaceDust key"),
            save->OverlayProfiles[0].Overlays.Contains(TEXT("SpaceDust")));
    }

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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesSaveOwedTest, "SOLTest.SettingsRules.SaveOwed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A write is owed after an Apply, a migration or a failed write, or while the applied overlay prefs differ from the
// written ones (SDD 34 section 5.4: owed || !AreEqual(applied, written)); O, Alt+O, a row and the cycle key all count
bool FSOLSettingsRulesSaveOwedTest::RunTest(const FString& /*parameters*/)
{
    using SOLSettingsRules::IsSaveOwed;
    using namespace SOLOverlay;

    // The truth table on equal and different prefs
    const FOverlayPrefs defaults = MakeDefaultPrefs();
    FOverlayPrefs rowOff = defaults;
    Toggle(rowOff, EOverlay::IconRings);
    TestFalse(TEXT("nothing owed, same prefs"), IsSaveOwed(false, defaults, defaults));
    TestTrue(TEXT("prefs differ (applied changed)"), IsSaveOwed(false, rowOff, defaults));
    TestTrue(TEXT("prefs differ (written changed)"), IsSaveOwed(false, defaults, rowOff));
    TestTrue(TEXT("owed, same prefs"), IsSaveOwed(true, defaults, defaults));
    TestTrue(TEXT("owed, prefs differ"), IsSaveOwed(true, rowOff, defaults));

    // A difference only in an unused slot owes nothing (AreEqual ignores it)
    FOverlayPrefs unusedSlot = defaults;
    unusedSlot.Masks[4] = 0u;
    TestFalse(TEXT("unused slot only: not owed"), IsSaveOwed(false, unusedSlot, defaults));

    // The subsystem's bookkeeping: loaded, nothing owed
    FOverlayPrefs written = defaults;
    FOverlayPrefs applied = written;
    bool bOwed = false;
    TestFalse(TEXT("after load: not owed"), IsSaveOwed(bOwed, applied, written));

    // O (the active profile's master, D10): once owes a write, twice returns to the written value
    ToggleMaster(applied);
    TestTrue(TEXT("after one O: owed"), IsSaveOwed(bOwed, applied, written));
    ToggleMaster(applied);
    TestFalse(TEXT("after O twice: not owed"), IsSaveOwed(bOwed, applied, written));

    // Alt+O (the active profile's orbit-lines switch, D18): the same
    ToggleOrbitLines(applied);
    TestTrue(TEXT("after one Alt+O: owed"), IsSaveOwed(bOwed, applied, written));
    ToggleOrbitLines(applied);
    TestFalse(TEXT("after Alt+O twice: not owed"), IsSaveOwed(bOwed, applied, written));

    // A menu row: the same
    Toggle(applied, EOverlay::Darkening);
    TestTrue(TEXT("after a row click: owed"), IsSaveOwed(bOwed, applied, written));
    Toggle(applied, EOverlay::Darkening);
    TestFalse(TEXT("after the row back: not owed"), IsSaveOwed(bOwed, applied, written));

    // Enabling a profile owes a write; once written, cycling to it owes one, and cycling back owes none
    SetProfileEnabled(applied, 1, true);
    TestTrue(TEXT("after enabling profile 2: owed"), IsSaveOwed(bOwed, applied, written));
    written = applied;
    TestFalse(TEXT("after its write: not owed"), IsSaveOwed(bOwed, applied, written));
    CycleActiveProfile(applied);
    TestTrue(TEXT("after a cycle: owed"), IsSaveOwed(bOwed, applied, written));
    CycleActiveProfile(applied);
    TestFalse(TEXT("after cycling back: not owed"), IsSaveOwed(bOwed, applied, written));

    // A change, then a successful write records the new prefs and clears the debt
    ToggleMaster(applied);
    TestTrue(TEXT("O once: owed"), IsSaveOwed(bOwed, applied, written));
    written = applied;
    bOwed = false;
    TestFalse(TEXT("after the write: not owed"), IsSaveOwed(bOwed, applied, written));

    // A migration owes one write back even with the prefs unchanged
    bOwed = true;
    TestTrue(TEXT("migration: owed"), IsSaveOwed(bOwed, applied, written));
    bOwed = false;
    TestFalse(TEXT("after the migration write: not owed"), IsSaveOwed(bOwed, applied, written));

    // A failed write keeps the debt even though applied now equals written
    bOwed = true;
    TestTrue(TEXT("failed write: owed with applied == written"), IsSaveOwed(bOwed, applied, written));
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
    TestTrue(TEXT("profile 1's orbit-lines switch shown after migration"),
        SOLOverlay::AreOrbitLinesOn(loaded->GetValues().OverlayPrefs));
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
    TestEqual(TEXT("v5 -> 10 (through 5 -> 6, 6 -> 7, 7 -> 8, 8 -> 9 and 9 -> 10)"), save->Version, 10);
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

    // A v6 save (which has the flag) keeps the player's choice through 6 -> 7 -> 8 -> 9 -> 10, and a current one across
    // two calls
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

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a per-profile bit set (enabled, used, master or orbit lines) has a profile's bit set
    bool SettingsRulesHasBit(const uint8 bits, const int32 profile)
    {
        return (static_cast<uint32>(bits) & (1u << static_cast<uint32>(profile))) != 0u;
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

    TestEqual(TEXT("v6 -> 10 (through 6 -> 7, 7 -> 8, 8 -> 9 and 9 -> 10)"), save->Version, 10);
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

    // The migrated save reads as the default overlay prefs, except profile 1's orbit-lines switch, which the 9 -> 10
    // step took from the hidden legacy flag (D18)
    const SOLOverlay::FOverlayPrefs prefs = save->GetValues().OverlayPrefs;
    SOLOverlay::FOverlayPrefs expectedPrefs = SOLOverlay::MakeDefaultPrefs();
    SOLOverlay::SetOrbitLinesOn(expectedPrefs, false);
    TestTrue(TEXT("reads as the default prefs with the switch off"), SOLOverlay::AreEqual(prefs, expectedPrefs));
    TestFalse(TEXT("profile 1's switch off"), SOLOverlay::AreOrbitLinesOn(prefs));
    TestTrue(TEXT("profile 1's master on"), SOLOverlay::IsMasterOn(prefs));
    TestTrue(TEXT("space dust on"), save->bSpaceDust);

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
// A current (schema 10) save is not migrated: its overlay profiles (flags included) and active profile stay exactly as
// they were
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
        TestFalse(TEXT("profile 1's enabled flag not set by a migration"), current->OverlayProfiles[1].bEnabled);
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
// GetValues (SDD 34 section 5.2): entry 0 is always enabled, entry p is enabled by bEnabled and used if enabled or its
// row map is not empty, a missing entry is disabled and unused, entries beyond MAX_PROFILES are ignored, and the
// masks, master bits, orbit-lines bits and active index are read as stored
bool FSOLSettingsRulesOverlaySaveValuesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;

    // A new save reads as the default prefs
    const USOLSettingsSave* fresh = NewObject<USOLSettingsSave>();
    TestEqual(TEXT("new save: no profiles stored"), fresh->OverlayProfiles.Num(), 0);
    TestEqual(TEXT("new save: active 0"), fresh->ActiveOverlayProfile, 0);
    const FOverlayPrefs freshPrefs = fresh->GetValues().OverlayPrefs;
    TestTrue(TEXT("empty: profile 1 enabled"), IsProfileEnabled(freshPrefs, 0));
    for (int32 profile = 1; profile < SOL::Overlay::MAX_PROFILES; ++profile)
    {
        const FString label = FString::Printf(TEXT("empty: profile %d"), profile + 1);
        TestFalse(label + TEXT(" disabled"), IsProfileEnabled(freshPrefs, profile));
        TestFalse(label + TEXT(" unused"), IsProfileUsed(freshPrefs, profile));
    }
    TestEqual(TEXT("empty: active 0"), freshPrefs.ActiveProfile, 0);
    TestEqual(TEXT("empty: default mask"), freshPrefs.Masks[0], DefaultMask());
    TestTrue(TEXT("empty: master on"), IsMasterOn(freshPrefs));
    TestTrue(TEXT("empty: orbit lines on"), AreOrbitLinesOn(freshPrefs));
    TestTrue(TEXT("empty: the default prefs"), AreEqual(freshPrefs, MakeDefaultPrefs()));

    // Four stored entries: profile 1 (its stored bEnabled ignored), profile 2 enabled with its master off, profile 3
    // disabled with a partial row map (used) and its switch off, profile 4 disabled with an empty map (unused);
    // profile 5 is missing
    USOLSettingsSave* four = NewObject<USOLSettingsSave>();
    FSOLOverlayProfileSave first = SettingsRulesOverlayProfile(0x11u);
    first.bEnabled = false;
    FSOLOverlayProfileSave second = SettingsRulesOverlayProfile(0x22u);
    second.bEnabled = true;
    second.bMasterOn = false;
    TMap<FString, bool> partial;
    partial.Add(TEXT("asteroidorbits"), true);
    FSOLOverlayProfileSave third;
    third.Overlays = partial;
    third.bEnabled = false;
    third.bOrbitLinesOn = false;
    FSOLOverlayProfileSave fourth;
    fourth.bEnabled = false;
    four->OverlayProfiles.Add(first);
    four->OverlayProfiles.Add(second);
    four->OverlayProfiles.Add(third);
    four->OverlayProfiles.Add(fourth);
    four->ActiveOverlayProfile = 1;
    const FOverlayPrefs fourPrefs = four->GetValues().OverlayPrefs;
    TestTrue(TEXT("four: profile 1 always enabled"), IsProfileEnabled(fourPrefs, 0));
    TestTrue(TEXT("four: profile 2 enabled"), IsProfileEnabled(fourPrefs, 1));
    TestFalse(TEXT("four: profile 3 disabled"), IsProfileEnabled(fourPrefs, 2));
    TestTrue(TEXT("four: profile 3 used (non-empty map)"), IsProfileUsed(fourPrefs, 2));
    TestFalse(TEXT("four: profile 4 disabled"), IsProfileEnabled(fourPrefs, 3));
    TestFalse(TEXT("four: profile 4 unused (empty map)"), IsProfileUsed(fourPrefs, 3));
    TestFalse(TEXT("four: missing profile 5 disabled"), IsProfileEnabled(fourPrefs, 4));
    TestFalse(TEXT("four: missing profile 5 unused"), IsProfileUsed(fourPrefs, 4));
    TestEqual(TEXT("four: active carried"), fourPrefs.ActiveProfile, 1);
    TestEqual(TEXT("four: mask 0"), fourPrefs.Masks[0], 0x11u);
    TestEqual(TEXT("four: mask 1"), fourPrefs.Masks[1], 0x22u);
    TestEqual(TEXT("four: partial profile fills defaults"), fourPrefs.Masks[2], FromSavedProfile(partial));
    TestEqual(TEXT("four: partial profile = default + asteroid orbits"), fourPrefs.Masks[2], AllBitsMask());
    TestTrue(TEXT("four: profile 1 master on"), SettingsRulesHasBit(fourPrefs.MasterOnProfiles, 0));
    TestFalse(TEXT("four: profile 2 master off"), SettingsRulesHasBit(fourPrefs.MasterOnProfiles, 1));
    TestTrue(TEXT("four: profile 3 master on"), SettingsRulesHasBit(fourPrefs.MasterOnProfiles, 2));
    TestTrue(TEXT("four: profile 1 orbit lines on"), SettingsRulesHasBit(fourPrefs.OrbitLinesProfiles, 0));
    TestTrue(TEXT("four: profile 2 orbit lines on"), SettingsRulesHasBit(fourPrefs.OrbitLinesProfiles, 1));
    TestFalse(TEXT("four: profile 3 orbit lines off"), SettingsRulesHasBit(fourPrefs.OrbitLinesProfiles, 2));
    TestFalse(TEXT("four: active profile 2's master reads off"), IsMasterOn(fourPrefs));

    // An enabled entry with an empty map is used and reads the default rows
    USOLSettingsSave* emptyEnabled = NewObject<USOLSettingsSave>();
    emptyEnabled->OverlayProfiles.Add(SettingsRulesOverlayProfile(0x3u));
    FSOLOverlayProfileSave enabledEmpty;
    enabledEmpty.bEnabled = true;
    emptyEnabled->OverlayProfiles.Add(enabledEmpty);
    const FOverlayPrefs emptyEnabledPrefs = emptyEnabled->GetValues().OverlayPrefs;
    TestTrue(TEXT("empty enabled: profile 2 enabled"), IsProfileEnabled(emptyEnabledPrefs, 1));
    TestTrue(TEXT("empty enabled: profile 2 used"), IsProfileUsed(emptyEnabledPrefs, 1));
    TestEqual(TEXT("empty enabled: profile 2 default rows"), emptyEnabledPrefs.Masks[1], DefaultMask());

    // More stored entries than MAX_PROFILES: the extra ones are ignored
    USOLSettingsSave* many = NewObject<USOLSettingsSave>();
    for (int32 index = 0; index < SOL::Overlay::MAX_PROFILES + 2; ++index)
    {
        FSOLOverlayProfileSave entry = SettingsRulesOverlayProfile(static_cast<uint32>(index));
        entry.bEnabled = true;
        many->OverlayProfiles.Add(entry);
    }
    const FOverlayPrefs manyPrefs = many->GetValues().OverlayPrefs;
    for (int32 profile = 0; profile < SOL::Overlay::MAX_PROFILES; ++profile)
    {
        TestTrue(FString::Printf(TEXT("many: profile %d enabled"), profile + 1), IsProfileEnabled(manyPrefs, profile));
    }
    TestEqual(TEXT("many: last kept mask"), manyPrefs.Masks[SOL::Overlay::MAX_PROFILES - 1],
        static_cast<uint32>(SOL::Overlay::MAX_PROFILES - 1));
    TestEqual(TEXT("many: no enabled bit beyond MAX_PROFILES"),
        static_cast<int32>(manyPrefs.EnabledProfiles) & ~((1 << SOL::Overlay::MAX_PROFILES) - 1), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesOverlaySaveWriteTest, "SOLTest.SettingsRules.OverlaySaveWrite",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SetValues (SDD 34 section 5.1) always writes MAX_PROFILES entries: a used slot with its full row map and its own
// enabled, master and orbit-lines flags, an unused slot with an empty row map and bEnabled false; GetValues gives the
// same prefs back
bool FSOLSettingsRulesOverlaySaveWriteTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOverlay;

    // Profile 1 edited with its orbit-lines switch off; profile 4 enabled, edited (master off) and disabled again (used
    // but disabled); profile 3 enabled, edited and active; profiles 2 and 5 never enabled (unused)
    SOLSettingsModel::FSettingsValues values;
    FOverlayPrefs& prefs = values.OverlayPrefs;
    prefs = MakeDefaultPrefs();
    SetEnabled(prefs, EOverlay::IconShips, false);
    SetOrbitLinesOn(prefs, false);
    SetProfileEnabled(prefs, 3, true);
    SetActiveProfile(prefs, 3);
    SetMasterOn(prefs, false);
    Toggle(prefs, EOverlay::PlanetOrbits);
    SetProfileEnabled(prefs, 3, false);
    SetProfileEnabled(prefs, 2, true);
    SetActiveProfile(prefs, 2);
    SetOrbitLinesOn(prefs, true);
    Toggle(prefs, EOverlay::IconRings);
    TestEqual(TEXT("fixture: profile 3 active"), prefs.ActiveProfile, 2);
    TestTrue(TEXT("fixture: profile 4 used"), IsProfileUsed(prefs, 3));
    TestFalse(TEXT("fixture: profile 4 disabled"), IsProfileEnabled(prefs, 3));

    USOLSettingsSave* save = NewObject<USOLSettingsSave>();
    save->SetValues(values);
    TestEqual(TEXT("SetValues: MAX_PROFILES entries"), save->OverlayProfiles.Num(), SOL::Overlay::MAX_PROFILES);
    TestEqual(TEXT("SetValues: five entries"), save->OverlayProfiles.Num(), 5);
    TestEqual(TEXT("SetValues: active 2"), save->ActiveOverlayProfile, 2);
    if (save->OverlayProfiles.Num() == SOL::Overlay::MAX_PROFILES)
    {
        const TArray<FSOLOverlayProfileSave>& stored = save->OverlayProfiles;

        // Profile 1: full map, switch off, master on
        TestTrue(TEXT("profile 1: full map"), SettingsRulesSameOverlayMap(stored[0].Overlays,
            ToSavedProfile(prefs.Masks[0])));
        TestFalse(TEXT("profile 1: switch off"), stored[0].bOrbitLinesOn);
        TestTrue(TEXT("profile 1: master on"), stored[0].bMasterOn);

        // Profile 2 and 5: unused, empty map, disabled
        TestFalse(TEXT("profile 2: disabled"), stored[1].bEnabled);
        TestEqual(TEXT("profile 2: empty map"), stored[1].Overlays.Num(), 0);
        TestFalse(TEXT("profile 5: disabled"), stored[4].bEnabled);
        TestEqual(TEXT("profile 5: empty map"), stored[4].Overlays.Num(), 0);

        // Profile 3: enabled, full map, switch on, master on (copied from profile 1, then the switch set on)
        TestTrue(TEXT("profile 3: enabled"), stored[2].bEnabled);
        TestTrue(TEXT("profile 3: full map"), SettingsRulesSameOverlayMap(stored[2].Overlays,
            ToSavedProfile(prefs.Masks[2])));
        TestTrue(TEXT("profile 3: switch on"), stored[2].bOrbitLinesOn);
        TestTrue(TEXT("profile 3: master on"), stored[2].bMasterOn);

        // Profile 4: used but disabled, full map kept with its own master and switch
        TestFalse(TEXT("profile 4: disabled"), stored[3].bEnabled);
        TestTrue(TEXT("profile 4: full map"), SettingsRulesSameOverlayMap(stored[3].Overlays,
            ToSavedProfile(prefs.Masks[3])));
        TestTrue(TEXT("profile 4: map not empty"), stored[3].Overlays.Num() > 0);
        TestFalse(TEXT("profile 4: master off"), stored[3].bMasterOn);
        TestFalse(TEXT("profile 4: switch off (copied from profile 1)"), stored[3].bOrbitLinesOn);
    }

    // GetValues reads every bit back
    const FOverlayPrefs read = save->GetValues().OverlayPrefs;
    TestTrue(TEXT("prefs round trip"), AreEqual(read, prefs));
    TestTrue(TEXT("values round trip"), SOLSettingsModel::AreEqual(save->GetValues(), values));
    const bool expectedEnabled[] = { true, false, true, false, false };
    const bool expectedUsed[] = { true, false, true, true, false };
    for (int32 profile = 0; profile < SOL::Overlay::MAX_PROFILES; ++profile)
    {
        const FString label = FString::Printf(TEXT("read: profile %d"), profile + 1);
        TestTrue(label + TEXT(" enabled as written"), IsProfileEnabled(read, profile) == expectedEnabled[profile]);
        TestTrue(label + TEXT(" used as written"), IsProfileUsed(read, profile) == expectedUsed[profile]);
    }
    TestEqual(TEXT("read: active 2"), read.ActiveProfile, 2);
    TestEqual(TEXT("read: profile 1 mask"), read.Masks[0], prefs.Masks[0]);
    TestEqual(TEXT("read: profile 3 mask"), read.Masks[2], prefs.Masks[2]);
    TestEqual(TEXT("read: profile 4 mask"), read.Masks[3], prefs.Masks[3]);
    TestFalse(TEXT("read: profile 4 master off"), SettingsRulesHasBit(read.MasterOnProfiles, 3));
    TestTrue(TEXT("read: profile 3 master on"), SettingsRulesHasBit(read.MasterOnProfiles, 2));
    TestFalse(TEXT("read: profile 1 switch off"), SettingsRulesHasBit(read.OrbitLinesProfiles, 0));
    TestTrue(TEXT("read: profile 3 switch on"), SettingsRulesHasBit(read.OrbitLinesProfiles, 2));
    TestFalse(TEXT("read: profile 4 switch off"), SettingsRulesHasBit(read.OrbitLinesProfiles, 3));

    // Writing the defaults later still writes five entries, the four unused ones empty, active 0
    values.OverlayPrefs = MakeDefaultPrefs();
    save->SetValues(values);
    TestEqual(TEXT("defaults: five entries"), save->OverlayProfiles.Num(), SOL::Overlay::MAX_PROFILES);
    TestEqual(TEXT("defaults: active 0"), save->ActiveOverlayProfile, 0);
    for (int32 index = 1; index < save->OverlayProfiles.Num(); ++index)
    {
        const FString label = FString::Printf(TEXT("defaults: profile %d"), index + 1);
        TestFalse(label + TEXT(" disabled"), save->OverlayProfiles[index].bEnabled);
        TestEqual(label + TEXT(" empty map"), save->OverlayProfiles[index].Overlays.Num(), 0);
    }
    TestTrue(TEXT("defaults: round trip"), AreEqual(save->GetValues().OverlayPrefs, MakeDefaultPrefs()));
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
    TestTrue(TEXT("a v7 save is migrated"), ClassifySaveVersion(7, SOL::Settings::SAVE_VERSION)
        == ESaveVersionAction::Migrate);
    TestTrue(TEXT("classify(8, 8) uses"), ClassifySaveVersion(8, 8) == ESaveVersionAction::Use);
    TestTrue(TEXT("a newer save is ignored"), ClassifySaveVersion(SOL::Settings::SAVE_VERSION + 1,
        SOL::Settings::SAVE_VERSION) == ESaveVersionAction::UseDefaults);

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
// Saves 1 to 7 reach the current schema with the default margin; a v8 save (migrated by 8 -> 9 and 9 -> 10 only) and a
// current one keep their margin; a newer save is left alone
bool FSOLSettingsRulesChaseMarginVersionsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsRules;

    // Every schema before the margin migrates and ends at the current one with the default margin
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
        TestEqual(label + TEXT(": ends at the current schema"), older->Version, SOL::Settings::SAVE_VERSION);
        TestEqual(label + TEXT(": default margin"), older->ChaseSafetyMargin,
            SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT, SETTINGSRULES_TEST_TOLERANCE);
        TestEqual(label + TEXT(": master kept"), older->MasterVolume, 0.45f, SETTINGSRULES_TEST_TOLERANCE);
    }

    // A schema 1 save goes through every step to the current schema and keeps its other fields
    USOLSettingsSave* v1 = NewObject<USOLSettingsSave>();
    v1->Version = 1;
    v1->MusicVolume = 0.2f;
    v1->KeyOverrides.Add(TEXT("Fire"), TEXT("F"));
    v1->MigrateToCurrentVersion();
    TestEqual(TEXT("v1 -> current"), v1->Version, SOL::Settings::SAVE_VERSION);
    TestEqual(TEXT("v1: margin 0.25"), v1->ChaseSafetyMargin, 0.25f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("v1: music kept"), v1->MusicVolume, 0.2f, SETTINGSRULES_TEST_TOLERANCE);
    const FString* fireKey = v1->KeyOverrides.Find(TEXT("Fire"));
    TestTrue(TEXT("v1: fire override kept"), fireKey != nullptr && *fireKey == TEXT("F"));
    TestEqual(TEXT("v1: one overlay profile"), v1->OverlayProfiles.Num(), 1);
    v1->MigrateToCurrentVersion();
    TestEqual(TEXT("v1: second migrate keeps the version"), v1->Version, SOL::Settings::SAVE_VERSION);
    TestEqual(TEXT("v1: second migrate keeps the margin"), v1->ChaseSafetyMargin, 0.25f, SETTINGSRULES_TEST_TOLERANCE);

    // A schema 8 save has the margin: only 8 -> 9 and 9 -> 10 run, and its margin is the player's choice
    USOLSettingsSave* v8 = NewObject<USOLSettingsSave>();
    v8->Version = 8;
    v8->ChaseSafetyMargin = 0.1f;
    v8->MigrateToCurrentVersion();
    TestEqual(TEXT("v8: migrated to the current schema"), v8->Version, SOL::Settings::SAVE_VERSION);
    TestEqual(TEXT("v8: margin kept"), v8->ChaseSafetyMargin, 0.1f, SETTINGSRULES_TEST_TOLERANCE);
    v8->ChaseSafetyMargin = 0.0f;
    v8->MigrateToCurrentVersion();
    TestEqual(TEXT("v8: margin 0 kept"), v8->ChaseSafetyMargin, 0.0f, SETTINGSRULES_TEST_TOLERANCE);

    // A current save is used as is
    USOLSettingsSave* current = NewObject<USOLSettingsSave>();
    current->Version = SOL::Settings::SAVE_VERSION;
    current->ChaseSafetyMargin = 0.1f;
    current->MigrateToCurrentVersion();
    TestEqual(TEXT("current: version kept"), current->Version, SOL::Settings::SAVE_VERSION);
    TestEqual(TEXT("current: margin kept"), current->ChaseSafetyMargin, 0.1f, SETTINGSRULES_TEST_TOLERANCE);

    // A newer or unversioned save is never migrated
    const int32 untouchedVersions[] = { 0, SOL::Settings::SAVE_VERSION + 1 };
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

// Issue #43 Space Dust overlay row in the settings save (schema 9, SDD 43 section 6.3), revalidated for issue #34
// (schema 10, SDD 34 section 5.3): the 8 -> 9 step still adds SpaceDust to every profile, and 9 -> 10 then moves it
// out of the profiles into the global bSpaceDust

namespace
{
    // The saved id of the old Space Dust overlay row, restated so the tests stay independent of the production table
    constexpr const TCHAR* SETTINGSRULES_DUST_ID = TEXT("SpaceDust");

    // The binding id of the old Shift+O darkening action that the 9 -> 10 step drops (D15)
    constexpr const TCHAR* SETTINGSRULES_DARKENING_KEY_ID = TEXT("ToggleDarkening");

    //////////////////////////////////////////////////////////////////////////
    // Returns one saved overlay profile holding exactly these entries
    FSOLOverlayProfileSave SettingsRulesOverlayProfileOf(const TMap<FString, bool>& overlays)
    {
        FSOLOverlayProfileSave profile;
        profile.Overlays = overlays;
        return profile;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a saved overlay map without the SpaceDust key (what the 9 -> 10 step leaves)
    TMap<FString, bool> SettingsRulesWithoutDust(const TMap<FString, bool>& overlays)
    {
        TMap<FString, bool> result = overlays;
        result.Remove(SETTINGSRULES_DUST_ID);
        return result;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a saved overlay map holds the SpaceDust key at all
    bool SettingsRulesHasDustKey(const TMap<FString, bool>& overlays)
    {
        return overlays.Contains(SETTINGSRULES_DUST_ID);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two key-override maps hold exactly the same ids and keys
    bool SettingsRulesSameOverrides(const TMap<FString, FString>& a, const TMap<FString, FString>& b)
    {
        if (a.Num() != b.Num())
        {
            return false;
        }
        for (const TPair<FString, FString>& entry : a)
        {
            const FString* other = b.Find(entry.Key);
            if (other == nullptr || *other != entry.Value)
            {
                return false;
            }
        }
        return true;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesMigrateV8SpaceDustTest, "SOLTest.SettingsRules.MigrateV8SpaceDust",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A v8 save goes through 8 -> 9 (SpaceDust added as on where missing) and 9 -> 10 (moved to the global flag): every
// profile ends as its old map with no SpaceDust key, enabled with its master on and its orbit-lines switch from the
// legacy flag; the active profile, the count and every other field are kept, and a second migrate changes nothing
bool FSOLSettingsRulesMigrateV8SpaceDustTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsRules;
    TestEqual(TEXT("schema version is 10"), SOL::Settings::SAVE_VERSION, 10);
    TestEqual(TEXT("key layout is 10"), SOL::Settings::KEY_BINDING_LAYOUT_VERSION, 10);
    TestTrue(TEXT("a v8 save is migrated"), ClassifySaveVersion(8, SOL::Settings::SAVE_VERSION)
        == ESaveVersionAction::Migrate);
    TestTrue(TEXT("a v9 save is migrated"), ClassifySaveVersion(9, SOL::Settings::SAVE_VERSION)
        == ESaveVersionAction::Migrate);
    TestTrue(TEXT("classify(10, 10) uses"), ClassifySaveVersion(10, 10) == ESaveVersionAction::Use);

    // Three pre-#43 profiles: partial maps (one with a row set false, one with an unknown key) and a full older one
    TMap<FString, bool> first;
    first.Add(TEXT("Icons"), true);
    first.Add(TEXT("AsteroidOrbits"), false);
    first.Add(TEXT("Darkening"), true);
    TMap<FString, bool> second;
    second.Add(TEXT("IconShips"), false);
    second.Add(TEXT("PlanetOrbits"), true);
    second.Add(TEXT("NoSuchOverlay"), false);
    const uint32 ringsBit = 1u << static_cast<uint32>(SOLOverlay::EOverlay::IconRings);
    TMap<FString, bool> third = SOLOverlay::ToSavedProfile(SOLOverlay::DefaultMask() & ~ringsBit);
    third.Remove(SETTINGSRULES_DUST_ID);
    const TArray<TMap<FString, bool>> before = { first, second, third };

    // A v8 save holding them, with profile 3 active, hidden legacy orbit lines and non-default values elsewhere
    USOLSettingsSave* save = NewObject<USOLSettingsSave>();
    save->Version = 8;
    for (const TMap<FString, bool>& overlays : before)
    {
        save->OverlayProfiles.Add(SettingsRulesOverlayProfileOf(overlays));
    }
    save->ActiveOverlayProfile = 2;
    save->ChaseSafetyMargin = 0.1f;
    save->MasterVolume = 0.3f;
    save->bShowOrbitLines = false;
    save->bSpaceDust = false;                                     // A stray value a v8 file never wrote
    save->KeyOverrides.Add(TEXT("Fire"), TEXT("F"));
    save->MigrateToCurrentVersion();

    TestEqual(TEXT("v8 -> 10"), save->Version, 10);
    TestEqual(TEXT("v8 -> current"), save->Version, SOL::Settings::SAVE_VERSION);
    TestEqual(TEXT("profile count kept (not padded)"), save->OverlayProfiles.Num(), before.Num());
    TestEqual(TEXT("active overlay profile kept"), save->ActiveOverlayProfile, 2);
    TestTrue(TEXT("global dust on (8 -> 9 added it as on to profile 1)"), save->bSpaceDust);

    // Each profile is exactly its old map, enabled, master on, switch from the hidden legacy flag
    const int32 checked = FMath::Min(save->OverlayProfiles.Num(), before.Num());
    for (int32 index = 0; index < checked; ++index)
    {
        const FSOLOverlayProfileSave& profile = save->OverlayProfiles[index];
        const FString label = FString::Printf(TEXT("profile %d"), index + 1);
        TestFalse(label + TEXT(": no SpaceDust key"), SettingsRulesHasDustKey(profile.Overlays));
        TestTrue(label + TEXT(": exactly its old map"), SettingsRulesSameOverlayMap(profile.Overlays, before[index]));
        TestTrue(label + TEXT(": enabled"), profile.bEnabled);
        TestTrue(label + TEXT(": master on"), profile.bMasterOn);
        TestFalse(label + TEXT(": orbit lines off from the legacy flag"), profile.bOrbitLinesOn);
    }

    // Every other field is untouched
    TestEqual(TEXT("margin kept"), save->ChaseSafetyMargin, 0.1f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("master kept"), save->MasterVolume, 0.3f, SETTINGSRULES_TEST_TOLERANCE);
    TestFalse(TEXT("legacy hidden orbit lines kept"), save->bShowOrbitLines);
    TestEqual(TEXT("override count kept"), save->KeyOverrides.Num(), 1);
    const FString* fireKey = save->KeyOverrides.Find(TEXT("Fire"));
    TestTrue(TEXT("fire override kept"), fireKey != nullptr && *fireKey == TEXT("F"));

    // The migrated save reads three enabled profiles, profile 3 active with its rings off, master on, switch off
    const SOLSettingsModel::FSettingsValues values = save->GetValues();
    const SOLOverlay::FOverlayPrefs& prefs = values.OverlayPrefs;
    for (int32 profile = 0; profile < SOL::Overlay::MAX_PROFILES; ++profile)
    {
        TestTrue(FString::Printf(TEXT("reads profile %d enabled as stored"), profile + 1),
            SOLOverlay::IsProfileEnabled(prefs, profile) == (profile < 3));
    }
    TestEqual(TEXT("reads profile 3 active"), prefs.ActiveProfile, 2);
    TestFalse(TEXT("rings off in the active profile"), SOLOverlay::IsEnabled(prefs, SOLOverlay::EOverlay::IconRings));
    TestTrue(TEXT("master on in the active profile"), SOLOverlay::IsMasterOn(prefs));
    TestFalse(TEXT("orbit lines off in the active profile"), SOLOverlay::AreOrbitLinesOn(prefs));
    TestTrue(TEXT("reads the global dust on"), values.bSpaceDust);

    // Idempotent: a second migrate changes nothing
    const TArray<FSOLOverlayProfileSave> afterFirst = save->OverlayProfiles;
    save->MigrateToCurrentVersion();
    TestEqual(TEXT("second migrate keeps the version"), save->Version, SOL::Settings::SAVE_VERSION);
    TestEqual(TEXT("second migrate keeps the count"), save->OverlayProfiles.Num(), afterFirst.Num());
    TestEqual(TEXT("second migrate keeps the active profile"), save->ActiveOverlayProfile, 2);
    TestTrue(TEXT("second migrate keeps the dust"), save->bSpaceDust);
    for (int32 index = 0; index < FMath::Min(save->OverlayProfiles.Num(), afterFirst.Num()); ++index)
    {
        TestTrue(FString::Printf(TEXT("second migrate keeps profile %d"), index + 1),
            SettingsRulesSameOverlayMap(save->OverlayProfiles[index].Overlays, afterFirst[index].Overlays));
    }

    // A v8 profile 1 that already holds SpaceDust = false keeps it through 8 -> 9, so the global dust migrates off;
    // its sibling without the key is not consulted
    TMap<FString, bool> dustOff;
    dustOff.Add(SETTINGSRULES_DUST_ID, false);
    dustOff.Add(TEXT("Icons"), true);
    USOLSettingsSave* chosen = NewObject<USOLSettingsSave>();
    chosen->Version = 8;
    chosen->OverlayProfiles.Add(SettingsRulesOverlayProfileOf(dustOff));
    chosen->OverlayProfiles.Add(SettingsRulesOverlayProfileOf(first));
    chosen->ActiveOverlayProfile = 1;
    chosen->MigrateToCurrentVersion();
    TestEqual(TEXT("chosen: v8 -> current"), chosen->Version, SOL::Settings::SAVE_VERSION);
    TestEqual(TEXT("chosen: two profiles"), chosen->OverlayProfiles.Num(), 2);
    TestEqual(TEXT("chosen: active kept"), chosen->ActiveOverlayProfile, 1);
    TestFalse(TEXT("chosen: global dust off from profile 1"), chosen->bSpaceDust);
    if (chosen->OverlayProfiles.Num() == 2)
    {
        TestTrue(TEXT("chosen: profile 1 without SpaceDust"),
            SettingsRulesSameOverlayMap(chosen->OverlayProfiles[0].Overlays, SettingsRulesWithoutDust(dustOff)));
        TestTrue(TEXT("chosen: profile 2 its old map"),
            SettingsRulesSameOverlayMap(chosen->OverlayProfiles[1].Overlays, first));
    }
    TestFalse(TEXT("chosen: reads the dust off"), chosen->GetValues().bSpaceDust);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesSpaceDustVersionsTest, "SOLTest.SettingsRules.SpaceDustVersions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Saves 1 to 9 end at the current schema with the global dust on and no SpaceDust key in any profile; a current save
// is left alone (its dust flag and any stray key kept); a v8 save without profiles gains none
bool FSOLSettingsRulesSpaceDustVersionsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsRules;

    // A v7 save with two stored profiles (kept by 7 -> 8) ends with both as they were, enabled
    TMap<FString, bool> iconsOff;
    iconsOff.Add(TEXT("Icons"), false);
    TMap<FString, bool> orbits;
    orbits.Add(TEXT("PlanetOrbits"), false);
    orbits.Add(TEXT("MoonOrbits"), true);
    USOLSettingsSave* v7 = NewObject<USOLSettingsSave>();
    v7->Version = 7;
    v7->OverlayProfiles.Add(SettingsRulesOverlayProfileOf(iconsOff));
    v7->OverlayProfiles.Add(SettingsRulesOverlayProfileOf(orbits));
    v7->ActiveOverlayProfile = 1;
    v7->MigrateToCurrentVersion();
    TestEqual(TEXT("v7 -> current"), v7->Version, SOL::Settings::SAVE_VERSION);
    TestEqual(TEXT("v7: two profiles"), v7->OverlayProfiles.Num(), 2);
    TestEqual(TEXT("v7: active kept"), v7->ActiveOverlayProfile, 1);
    TestTrue(TEXT("v7: dust on"), v7->bSpaceDust);
    if (v7->OverlayProfiles.Num() == 2)
    {
        TestTrue(TEXT("v7: profile 1 as it was"), SettingsRulesSameOverlayMap(v7->OverlayProfiles[0].Overlays,
            iconsOff));
        TestTrue(TEXT("v7: profile 2 as it was"), SettingsRulesSameOverlayMap(v7->OverlayProfiles[1].Overlays,
            orbits));
        TestTrue(TEXT("v7: profile 1 enabled"), v7->OverlayProfiles[0].bEnabled);
        TestTrue(TEXT("v7: profile 2 enabled"), v7->OverlayProfiles[1].bEnabled);
    }

    // Every older schema ends at the current one with the dust on and no SpaceDust key left
    for (int32 version = 1; version <= 9; ++version)
    {
        const FString label = FString::Printf(TEXT("v%d"), version);
        USOLSettingsSave* older = NewObject<USOLSettingsSave>();
        older->Version = version;
        older->OverlayProfiles.Add(SettingsRulesOverlayProfileOf(iconsOff));
        older->bSpaceDust = false;                                // A stray value no older file wrote
        older->MigrateToCurrentVersion();
        TestEqual(label + TEXT(": ends at the current schema"), older->Version, SOL::Settings::SAVE_VERSION);
        TestTrue(label + TEXT(": dust on"), older->bSpaceDust);
        TestTrue(label + TEXT(": has a profile"), older->OverlayProfiles.Num() > 0);
        for (int32 index = 0; index < older->OverlayProfiles.Num(); ++index)
        {
            const FString profileLabel = label + FString::Printf(TEXT(": profile %d"), index + 1);
            TestFalse(profileLabel + TEXT(" has no SpaceDust key"),
                SettingsRulesHasDustKey(older->OverlayProfiles[index].Overlays));
            TestTrue(profileLabel + TEXT(" enabled"), older->OverlayProfiles[index].bEnabled);
        }
    }

    // A current save is not migrated: its dust flag and a stray SpaceDust key stay as they are
    TMap<FString, bool> strayDust = iconsOff;
    strayDust.Add(SETTINGSRULES_DUST_ID, true);
    USOLSettingsSave* current = NewObject<USOLSettingsSave>();
    current->Version = SOL::Settings::SAVE_VERSION;
    current->OverlayProfiles.Add(SettingsRulesOverlayProfileOf(strayDust));
    current->OverlayProfiles.Add(SettingsRulesOverlayProfileOf(orbits));
    current->ActiveOverlayProfile = 1;
    current->bSpaceDust = false;
    current->MigrateToCurrentVersion();
    TestEqual(TEXT("current: version kept"), current->Version, SOL::Settings::SAVE_VERSION);
    TestEqual(TEXT("current: two profiles"), current->OverlayProfiles.Num(), 2);
    TestEqual(TEXT("current: active kept"), current->ActiveOverlayProfile, 1);
    TestFalse(TEXT("current: dust off kept"), current->bSpaceDust);
    TestFalse(TEXT("current: reads the dust off"), current->GetValues().bSpaceDust);
    if (current->OverlayProfiles.Num() == 2)
    {
        TestTrue(TEXT("current: profile 1 unchanged"), SettingsRulesSameOverlayMap(current->OverlayProfiles[0].Overlays,
            strayDust));
        TestTrue(TEXT("current: profile 2 unchanged"), SettingsRulesSameOverlayMap(current->OverlayProfiles[1].Overlays,
            orbits));
    }

    // A v8 save without overlay profiles migrates without inventing one, and its dust is the default (on)
    USOLSettingsSave* empty = NewObject<USOLSettingsSave>();
    empty->Version = 8;
    empty->bSpaceDust = false;
    empty->MigrateToCurrentVersion();
    TestEqual(TEXT("empty: v8 -> current"), empty->Version, SOL::Settings::SAVE_VERSION);
    TestEqual(TEXT("empty: still no profiles"), empty->OverlayProfiles.Num(), 0);
    TestEqual(TEXT("empty: active 0 kept"), empty->ActiveOverlayProfile, 0);
    TestTrue(TEXT("empty: dust on"), empty->bSpaceDust);
    TestTrue(TEXT("empty: reads the dust on"), empty->GetValues().bSpaceDust);
    return true;
}

// Issue #34 overlay profiles in the settings save (schema 10), written against the SDD 34 section 5 contract before the
// production code

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesMigrateV9ProfilesTest, "SOLTest.SettingsRules.MigrateV9Profiles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Schema 9 -> 10 (issue #34): the global dust comes from profile 1's SpaceDust, SpaceDust leaves every profile (other
// keys kept), every stored entry is enabled with its master on and its orbit-lines switch from the legacy flag, the
// entries are not padded, the active profile is kept, the ToggleDarkening override is dropped (others verbatim), every
// other field is kept, and a second migrate changes nothing
bool FSOLSettingsRulesMigrateV9ProfilesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsRules;
    TestTrue(TEXT("a v9 save is migrated"), ClassifySaveVersion(9, SOL::Settings::SAVE_VERSION)
        == ESaveVersionAction::Migrate);

    // Two v9 profiles: profile 1 with the dust off, profile 2 with the dust on and an unknown key
    TMap<FString, bool> first;
    first.Add(TEXT("Icons"), true);
    first.Add(TEXT("AsteroidOrbits"), true);
    first.Add(TEXT("Darkening"), false);
    first.Add(SETTINGSRULES_DUST_ID, false);
    TMap<FString, bool> second;
    second.Add(SETTINGSRULES_DUST_ID, true);
    second.Add(TEXT("IconShips"), false);
    second.Add(TEXT("NoSuchOverlay"), true);
    const TArray<TMap<FString, bool>> before = { first, second };

    // A v9 save holding them with stray flag values a v9 file never wrote, profile 2 active, the legacy orbit lines
    // hidden, a darkening key override and non-default values elsewhere
    USOLSettingsSave* save = NewObject<USOLSettingsSave>();
    save->Version = 9;
    for (const TMap<FString, bool>& overlays : before)
    {
        FSOLOverlayProfileSave profile = SettingsRulesOverlayProfileOf(overlays);
        profile.bEnabled = false;
        profile.bMasterOn = false;
        profile.bOrbitLinesOn = true;
        save->OverlayProfiles.Add(profile);
    }
    save->ActiveOverlayProfile = 1;
    save->bShowOrbitLines = false;
    save->bSpaceDust = true;
    save->KeyOverrides.Add(SETTINGSRULES_DARKENING_KEY_ID, TEXT("P"));
    save->KeyOverrides.Add(TEXT("Fire"), TEXT("F"));
    save->KeyOverrides.Add(TEXT("ToggleOrbitLines"), TEXT("K"));
    save->ChaseSafetyMargin = 0.1f;
    save->MasterVolume = 0.3f;
    save->HudEffectsStrength = 0.4f;
    save->bMouseSteerAtStart = true;
    save->QualityPreset = 1;
    save->bVsyncChosen = true;
    save->MigrateToCurrentVersion();

    TestEqual(TEXT("v9 -> 10"), save->Version, 10);
    TestEqual(TEXT("v9 -> current"), save->Version, SOL::Settings::SAVE_VERSION);
    TestFalse(TEXT("global dust from profile 1 (off)"), save->bSpaceDust);
    TestEqual(TEXT("entries not padded"), save->OverlayProfiles.Num(), 2);
    TestEqual(TEXT("active profile kept"), save->ActiveOverlayProfile, 1);

    // Each stored entry: its old map without SpaceDust, enabled, master on, switch off from the legacy flag
    for (int32 index = 0; index < FMath::Min(save->OverlayProfiles.Num(), before.Num()); ++index)
    {
        const FSOLOverlayProfileSave& profile = save->OverlayProfiles[index];
        const FString label = FString::Printf(TEXT("profile %d"), index + 1);
        TestFalse(label + TEXT(": SpaceDust removed"), SettingsRulesHasDustKey(profile.Overlays));
        TestTrue(label + TEXT(": every other key kept"), SettingsRulesSameOverlayMap(profile.Overlays,
            SettingsRulesWithoutDust(before[index])));
        TestEqual(label + TEXT(": one key fewer"), profile.Overlays.Num(), before[index].Num() - 1);
        TestTrue(label + TEXT(": enabled"), profile.bEnabled);
        TestTrue(label + TEXT(": master on"), profile.bMasterOn);
        TestFalse(label + TEXT(": orbit lines off from the legacy flag"), profile.bOrbitLinesOn);
    }

    // The darkening override is dropped, the others kept verbatim
    TestNull(TEXT("ToggleDarkening override dropped"), save->KeyOverrides.Find(SETTINGSRULES_DARKENING_KEY_ID));
    TestEqual(TEXT("two overrides left"), save->KeyOverrides.Num(), 2);
    const FString* fireKey = save->KeyOverrides.Find(TEXT("Fire"));
    TestTrue(TEXT("fire override kept"), fireKey != nullptr && *fireKey == TEXT("F"));
    const FString* orbitKey = save->KeyOverrides.Find(TEXT("ToggleOrbitLines"));
    TestTrue(TEXT("orbit-lines override kept"), orbitKey != nullptr && *orbitKey == TEXT("K"));

    // Every other field is untouched, the legacy orbit-lines flag included
    TestFalse(TEXT("legacy orbit-lines flag left as it is"), save->bShowOrbitLines);
    TestEqual(TEXT("margin kept"), save->ChaseSafetyMargin, 0.1f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("master kept"), save->MasterVolume, 0.3f, SETTINGSRULES_TEST_TOLERANCE);
    TestEqual(TEXT("HUD effects kept"), save->HudEffectsStrength, 0.4f, SETTINGSRULES_TEST_TOLERANCE);
    TestTrue(TEXT("steer at start kept"), save->bMouseSteerAtStart);
    TestEqual(TEXT("quality kept"), save->QualityPreset, 1);
    TestTrue(TEXT("vsync chosen kept"), save->bVsyncChosen);

    // The migrated save reads profiles 1 and 2 enabled (2 active), 3 to 5 disabled and unused, masters on, switches
    // off, profile 1's darkening off and the dust off
    const SOLSettingsModel::FSettingsValues values = save->GetValues();
    const SOLOverlay::FOverlayPrefs& prefs = values.OverlayPrefs;
    TestTrue(TEXT("reads profile 1 enabled"), SOLOverlay::IsProfileEnabled(prefs, 0));
    TestTrue(TEXT("reads profile 2 enabled"), SOLOverlay::IsProfileEnabled(prefs, 1));
    for (int32 profile = 2; profile < SOL::Overlay::MAX_PROFILES; ++profile)
    {
        const FString label = FString::Printf(TEXT("reads profile %d"), profile + 1);
        TestFalse(label + TEXT(" disabled"), SOLOverlay::IsProfileEnabled(prefs, profile));
        TestFalse(label + TEXT(" unused"), SOLOverlay::IsProfileUsed(prefs, profile));
    }
    TestEqual(TEXT("reads profile 2 active"), prefs.ActiveProfile, 1);
    TestTrue(TEXT("reads profile 1 master on"), SettingsRulesHasBit(prefs.MasterOnProfiles, 0));
    TestTrue(TEXT("reads profile 2 master on"), SettingsRulesHasBit(prefs.MasterOnProfiles, 1));
    TestFalse(TEXT("reads profile 1 switch off"), SettingsRulesHasBit(prefs.OrbitLinesProfiles, 0));
    TestFalse(TEXT("reads profile 2 switch off"), SettingsRulesHasBit(prefs.OrbitLinesProfiles, 1));
    const uint32 darkeningBit = 1u << static_cast<uint32>(SOLOverlay::EOverlay::Darkening);
    TestEqual(TEXT("reads profile 1 darkening off"), prefs.Masks[0] & darkeningBit, 0u);
    TestFalse(TEXT("reads the dust off"), values.bSpaceDust);

    // Idempotent: a second migrate changes nothing
    const TArray<FSOLOverlayProfileSave> afterFirst = save->OverlayProfiles;
    const TMap<FString, FString> overridesAfterFirst = save->KeyOverrides;
    save->MigrateToCurrentVersion();
    TestEqual(TEXT("second migrate keeps the version"), save->Version, SOL::Settings::SAVE_VERSION);
    TestFalse(TEXT("second migrate keeps the dust"), save->bSpaceDust);
    TestEqual(TEXT("second migrate keeps the count"), save->OverlayProfiles.Num(), afterFirst.Num());
    TestEqual(TEXT("second migrate keeps the active profile"), save->ActiveOverlayProfile, 1);
    TestTrue(TEXT("second migrate keeps the overrides"), SettingsRulesSameOverrides(save->KeyOverrides,
        overridesAfterFirst));
    for (int32 index = 0; index < FMath::Min(save->OverlayProfiles.Num(), afterFirst.Num()); ++index)
    {
        const FSOLOverlayProfileSave& profile = save->OverlayProfiles[index];
        const FString label = FString::Printf(TEXT("second migrate: profile %d"), index + 1);
        TestTrue(label + TEXT(" map kept"), SettingsRulesSameOverlayMap(profile.Overlays, afterFirst[index].Overlays));
        TestTrue(label + TEXT(" flags kept"), profile.bEnabled == afterFirst[index].bEnabled
            && profile.bMasterOn == afterFirst[index].bMasterOn
            && profile.bOrbitLinesOn == afterFirst[index].bOrbitLinesOn);
    }

    // Later changes by the player survive another migrate (the step is skipped at schema 10)
    save->bSpaceDust = true;
    save->OverlayProfiles[1].bEnabled = false;
    save->OverlayProfiles[1].bMasterOn = false;
    save->OverlayProfiles[0].bOrbitLinesOn = true;
    save->MigrateToCurrentVersion();
    TestTrue(TEXT("a later dust change survives"), save->bSpaceDust);
    TestFalse(TEXT("a later disable survives"), save->OverlayProfiles[1].bEnabled);
    TestFalse(TEXT("a later master change survives"), save->OverlayProfiles[1].bMasterOn);
    TestTrue(TEXT("a later switch change survives"), save->OverlayProfiles[0].bOrbitLinesOn);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesMigrateV9DustSourceTest, "SOLTest.SettingsRules.MigrateV9DustSource",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The 9 -> 10 global dust is profile 1's SpaceDust value (true or false, whatever the other profiles hold), and true
// when profile 1 lacks the key or there are no profiles; the stored bSpaceDust is never what decides
bool FSOLSettingsRulesMigrateV9DustSourceTest::RunTest(const FString& /*parameters*/)
{
    TMap<FString, bool> dustOn;
    dustOn.Add(SETTINGSRULES_DUST_ID, true);
    dustOn.Add(TEXT("Icons"), false);
    TMap<FString, bool> dustOff;
    dustOff.Add(SETTINGSRULES_DUST_ID, false);
    dustOff.Add(TEXT("PlanetOrbits"), true);
    TMap<FString, bool> noDust;
    noDust.Add(TEXT("IconStars"), false);

    // Profile 1 on, profile 2 off: on
    USOLSettingsSave* on = NewObject<USOLSettingsSave>();
    on->Version = 9;
    on->OverlayProfiles.Add(SettingsRulesOverlayProfileOf(dustOn));
    on->OverlayProfiles.Add(SettingsRulesOverlayProfileOf(dustOff));
    on->bSpaceDust = false;
    on->MigrateToCurrentVersion();
    TestEqual(TEXT("on: v9 -> current"), on->Version, SOL::Settings::SAVE_VERSION);
    TestTrue(TEXT("on: dust from profile 1"), on->bSpaceDust);

    // Profile 1 off, profile 2 on: off
    USOLSettingsSave* off = NewObject<USOLSettingsSave>();
    off->Version = 9;
    off->OverlayProfiles.Add(SettingsRulesOverlayProfileOf(dustOff));
    off->OverlayProfiles.Add(SettingsRulesOverlayProfileOf(dustOn));
    off->bSpaceDust = true;
    off->MigrateToCurrentVersion();
    TestEqual(TEXT("off: v9 -> current"), off->Version, SOL::Settings::SAVE_VERSION);
    TestFalse(TEXT("off: dust from profile 1"), off->bSpaceDust);
    TestFalse(TEXT("off: reads the dust off"), off->GetValues().bSpaceDust);

    // Profile 1 without the key, profile 2 off: the default (on)
    USOLSettingsSave* missing = NewObject<USOLSettingsSave>();
    missing->Version = 9;
    missing->OverlayProfiles.Add(SettingsRulesOverlayProfileOf(noDust));
    missing->OverlayProfiles.Add(SettingsRulesOverlayProfileOf(dustOff));
    missing->bSpaceDust = false;
    missing->MigrateToCurrentVersion();
    TestEqual(TEXT("missing key: v9 -> current"), missing->Version, SOL::Settings::SAVE_VERSION);
    TestTrue(TEXT("missing key: dust on"), missing->bSpaceDust);
    TestTrue(TEXT("missing key: default constant"), missing->bSpaceDust == SOL::Settings::SPACE_DUST_DEFAULT);
    if (missing->OverlayProfiles.Num() == 2)
    {
        TestTrue(TEXT("missing key: profile 1 map unchanged"),
            SettingsRulesSameOverlayMap(missing->OverlayProfiles[0].Overlays, noDust));
        TestTrue(TEXT("missing key: profile 2 loses SpaceDust"),
            SettingsRulesSameOverlayMap(missing->OverlayProfiles[1].Overlays, SettingsRulesWithoutDust(dustOff)));
    }

    // No profiles at all: the default (on), and no profile is invented
    USOLSettingsSave* none = NewObject<USOLSettingsSave>();
    none->Version = 9;
    none->bSpaceDust = false;
    none->MigrateToCurrentVersion();
    TestEqual(TEXT("no profiles: v9 -> current"), none->Version, SOL::Settings::SAVE_VERSION);
    TestTrue(TEXT("no profiles: dust on"), none->bSpaceDust);
    TestEqual(TEXT("no profiles: none invented"), none->OverlayProfiles.Num(), 0);
    TestEqual(TEXT("no profiles: active 0 kept"), none->ActiveOverlayProfile, 0);

    // In every case SpaceDust is gone from every profile
    const USOLSettingsSave* migrated[] = { on, off, missing };
    for (const USOLSettingsSave* save : migrated)
    {
        for (int32 index = 0; index < save->OverlayProfiles.Num(); ++index)
        {
            TestFalse(FString::Printf(TEXT("profile %d has no SpaceDust key"), index + 1),
                SettingsRulesHasDustKey(save->OverlayProfiles[index].Overlays));
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesMigrateV9OrbitLinesTest, "SOLTest.SettingsRules.MigrateV9OrbitLines",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The 9 -> 10 step copies the file's global bShowOrbitLines into every stored profile's bOrbitLinesOn (D18), shown and
// hidden alike, and leaves the legacy field as it is
bool FSOLSettingsRulesMigrateV9OrbitLinesTest::RunTest(const FString& /*parameters*/)
{
    TMap<FString, bool> iconsOff;
    iconsOff.Add(TEXT("Icons"), false);
    const bool legacyValues[] = { true, false };
    for (const bool bLegacy : legacyValues)
    {
        const FString label = bLegacy ? TEXT("shown") : TEXT("hidden");

        // Three entries holding the opposite stray switch value
        USOLSettingsSave* save = NewObject<USOLSettingsSave>();
        save->Version = 9;
        for (int32 index = 0; index < 3; ++index)
        {
            FSOLOverlayProfileSave profile = SettingsRulesOverlayProfileOf(iconsOff);
            profile.bOrbitLinesOn = !bLegacy;
            save->OverlayProfiles.Add(profile);
        }
        save->ActiveOverlayProfile = 2;
        save->bShowOrbitLines = bLegacy;
        save->MigrateToCurrentVersion();

        TestEqual(label + TEXT(": v9 -> current"), save->Version, SOL::Settings::SAVE_VERSION);
        TestEqual(label + TEXT(": three entries"), save->OverlayProfiles.Num(), 3);
        TestTrue(label + TEXT(": legacy field left as it is"), save->bShowOrbitLines == bLegacy);
        for (int32 index = 0; index < save->OverlayProfiles.Num(); ++index)
        {
            TestTrue(label + FString::Printf(TEXT(": profile %d switch from the legacy flag"), index + 1),
                save->OverlayProfiles[index].bOrbitLinesOn == bLegacy);
        }

        // Read back: every stored profile's switch, and the active profile's
        const SOLOverlay::FOverlayPrefs prefs = save->GetValues().OverlayPrefs;
        for (int32 profile = 0; profile < 3; ++profile)
        {
            TestTrue(label + FString::Printf(TEXT(": reads profile %d switch"), profile + 1),
                SettingsRulesHasBit(prefs.OrbitLinesProfiles, profile) == bLegacy);
        }
        TestEqual(label + TEXT(": profile 3 active"), prefs.ActiveProfile, 2);
        TestTrue(label + TEXT(": active profile's switch"), SOLOverlay::AreOrbitLinesOn(prefs) == bLegacy);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesMigrateV9KeyOverridesTest,
    "SOLTest.SettingsRules.MigrateV9KeyOverrides",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The 9 -> 10 step drops a ToggleDarkening key override (the action is gone, D15) and keeps every other override
// verbatim; older saves lose it through the chain, a current save is never touched
bool FSOLSettingsRulesMigrateV9KeyOverridesTest::RunTest(const FString& /*parameters*/)
{
    // A v9 save with the darkening override among others
    USOLSettingsSave* save = NewObject<USOLSettingsSave>();
    save->Version = 9;
    save->KeyOverrides.Add(SETTINGSRULES_DARKENING_KEY_ID, TEXT("P"));
    save->KeyOverrides.Add(TEXT("Fire"), TEXT("F"));
    save->KeyOverrides.Add(TEXT("ToggleOrbitLines"), TEXT("K"));
    save->KeyOverrides.Add(TEXT("OpenMapJump"), TEXT("Comma"));
    TMap<FString, FString> expected = save->KeyOverrides;
    expected.Remove(SETTINGSRULES_DARKENING_KEY_ID);
    save->MigrateToCurrentVersion();
    TestEqual(TEXT("v9 -> current"), save->Version, SOL::Settings::SAVE_VERSION);
    TestNull(TEXT("darkening override dropped"), save->KeyOverrides.Find(SETTINGSRULES_DARKENING_KEY_ID));
    TestTrue(TEXT("the others verbatim"), SettingsRulesSameOverrides(save->KeyOverrides, expected));
    TestEqual(TEXT("three overrides left"), save->KeyOverrides.Num(), 3);

    // A v9 save without it keeps its overrides exactly
    USOLSettingsSave* plain = NewObject<USOLSettingsSave>();
    plain->Version = 9;
    plain->KeyOverrides.Add(TEXT("Boost"), TEXT("RightShift"));
    const TMap<FString, FString> plainBefore = plain->KeyOverrides;
    plain->MigrateToCurrentVersion();
    TestTrue(TEXT("plain: overrides unchanged"), SettingsRulesSameOverrides(plain->KeyOverrides, plainBefore));

    // A v9 save whose only override is the darkening one ends with none
    USOLSettingsSave* only = NewObject<USOLSettingsSave>();
    only->Version = 9;
    only->KeyOverrides.Add(SETTINGSRULES_DARKENING_KEY_ID, TEXT("P"));
    only->MigrateToCurrentVersion();
    TestEqual(TEXT("only: no override left"), only->KeyOverrides.Num(), 0);

    // A v7 save (the layout that introduced the darkening key) loses it through the chain, the rest kept
    USOLSettingsSave* v7 = NewObject<USOLSettingsSave>();
    v7->Version = 7;
    v7->KeyOverrides.Add(SETTINGSRULES_DARKENING_KEY_ID, TEXT("L"));
    v7->KeyOverrides.Add(TEXT("Fire"), TEXT("F"));
    v7->MigrateToCurrentVersion();
    TestEqual(TEXT("v7 -> current"), v7->Version, SOL::Settings::SAVE_VERSION);
    TestNull(TEXT("v7: darkening override dropped"), v7->KeyOverrides.Find(SETTINGSRULES_DARKENING_KEY_ID));
    const FString* fireKey = v7->KeyOverrides.Find(TEXT("Fire"));
    TestTrue(TEXT("v7: fire override kept"), fireKey != nullptr && *fireKey == TEXT("F"));
    TestEqual(TEXT("v7: one override left"), v7->KeyOverrides.Num(), 1);

    // A current save holding a stray darkening override is not migrated, so it is left untouched
    USOLSettingsSave* current = NewObject<USOLSettingsSave>();
    current->Version = SOL::Settings::SAVE_VERSION;
    current->KeyOverrides.Add(SETTINGSRULES_DARKENING_KEY_ID, TEXT("P"));
    current->MigrateToCurrentVersion();
    TestNotNull(TEXT("current: untouched"), current->KeyOverrides.Find(SETTINGSRULES_DARKENING_KEY_ID));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesMigrateChainToProfilesTest,
    "SOLTest.SettingsRules.MigrateChainToProfiles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A v6 and a v8 save go through the whole chain to schema 10: dust on, profile 1 enabled with its master on, no
// SpaceDust key anywhere, and the orbit-lines switch from the legacy flag
bool FSOLSettingsRulesMigrateChainToProfilesTest::RunTest(const FString& /*parameters*/)
{
    // A v6 save (no overlay profiles in its schema; stray ones are replaced by 6 -> 7) with the legacy lines shown
    USOLSettingsSave* v6 = NewObject<USOLSettingsSave>();
    v6->Version = 6;
    v6->OverlayProfiles.Add(SettingsRulesOverlayProfile(0u));
    v6->OverlayProfiles.Add(SettingsRulesOverlayProfile(0u));
    v6->ActiveOverlayProfile = 1;
    v6->bShowOrbitLines = true;
    v6->bSpaceDust = false;
    v6->MigrateToCurrentVersion();
    TestEqual(TEXT("v6 -> 10"), v6->Version, 10);
    TestTrue(TEXT("v6: dust on"), v6->bSpaceDust);
    TestEqual(TEXT("v6: one profile"), v6->OverlayProfiles.Num(), 1);
    TestEqual(TEXT("v6: active 0"), v6->ActiveOverlayProfile, 0);
    if (v6->OverlayProfiles.Num() == 1)
    {
        const FSOLOverlayProfileSave& profile = v6->OverlayProfiles[0];
        TestTrue(TEXT("v6: profile 1 enabled"), profile.bEnabled);
        TestTrue(TEXT("v6: profile 1 master on"), profile.bMasterOn);
        TestTrue(TEXT("v6: profile 1 switch on"), profile.bOrbitLinesOn);
        TestFalse(TEXT("v6: no SpaceDust key"), SettingsRulesHasDustKey(profile.Overlays));
        TestTrue(TEXT("v6: the default rows"), SettingsRulesSameOverlayMap(profile.Overlays,
            SOLOverlay::ToSavedProfile(SOLOverlay::DefaultMask())));
    }
    const SOLSettingsModel::FSettingsValues v6Values = v6->GetValues();
    TestTrue(TEXT("v6: reads profile 1 enabled"), SOLOverlay::IsProfileEnabled(v6Values.OverlayPrefs, 0));
    TestTrue(TEXT("v6: reads the default prefs"), SOLOverlay::AreEqual(v6Values.OverlayPrefs,
        SOLOverlay::MakeDefaultPrefs()));
    TestTrue(TEXT("v6: reads the dust on"), v6Values.bSpaceDust);

    // A v8 save with one profile and the legacy lines hidden
    TMap<FString, bool> iconsOff;
    iconsOff.Add(TEXT("Icons"), false);
    USOLSettingsSave* v8 = NewObject<USOLSettingsSave>();
    v8->Version = 8;
    v8->OverlayProfiles.Add(SettingsRulesOverlayProfileOf(iconsOff));
    v8->bShowOrbitLines = false;
    v8->bSpaceDust = false;
    v8->MigrateToCurrentVersion();
    TestEqual(TEXT("v8 -> 10"), v8->Version, 10);
    TestTrue(TEXT("v8: dust on"), v8->bSpaceDust);
    TestEqual(TEXT("v8: one profile"), v8->OverlayProfiles.Num(), 1);
    if (v8->OverlayProfiles.Num() == 1)
    {
        const FSOLOverlayProfileSave& profile = v8->OverlayProfiles[0];
        TestTrue(TEXT("v8: profile 1 enabled"), profile.bEnabled);
        TestTrue(TEXT("v8: profile 1 master on"), profile.bMasterOn);
        TestFalse(TEXT("v8: profile 1 switch off"), profile.bOrbitLinesOn);
        TestFalse(TEXT("v8: no SpaceDust key"), SettingsRulesHasDustKey(profile.Overlays));
        TestTrue(TEXT("v8: its old map"), SettingsRulesSameOverlayMap(profile.Overlays, iconsOff));
    }
    const SOLSettingsModel::FSettingsValues v8Values = v8->GetValues();
    TestTrue(TEXT("v8: reads profile 1 enabled"), SOLOverlay::IsProfileEnabled(v8Values.OverlayPrefs, 0));
    TestFalse(TEXT("v8: reads icons off"), SOLOverlay::IsEnabled(v8Values.OverlayPrefs, SOLOverlay::EOverlay::Icons));
    TestFalse(TEXT("v8: reads the switch off"), SOLOverlay::AreOrbitLinesOn(v8Values.OverlayPrefs));
    TestTrue(TEXT("v8: reads the master on"), SOLOverlay::IsMasterOn(v8Values.OverlayPrefs));
    TestTrue(TEXT("v8: reads the dust on"), v8Values.bSpaceDust);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsRulesSpaceDustSaveValuesTest, "SOLTest.SettingsRules.SpaceDustSaveValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A new save holds the dust on, SetValues / GetValues round-trip the global flag, and the dust never lands in a
// profile's row map (D5)
bool FSOLSettingsRulesSpaceDustSaveValuesTest::RunTest(const FString& /*parameters*/)
{
    const USOLSettingsSave* fresh = NewObject<USOLSettingsSave>();
    TestTrue(TEXT("new save: dust follows the constant"), fresh->bSpaceDust == SOL::Settings::SPACE_DUST_DEFAULT);
    TestTrue(TEXT("new save: dust on"), fresh->bSpaceDust);
    TestTrue(TEXT("new save: GetValues dust on"), fresh->GetValues().bSpaceDust);

    // Off round-trips as off
    USOLSettingsSave* save = NewObject<USOLSettingsSave>();
    SOLSettingsModel::FSettingsValues values;
    values.bSpaceDust = false;
    save->SetValues(values);
    TestFalse(TEXT("SetValues stores off"), save->bSpaceDust);
    TestFalse(TEXT("GetValues gives off back"), save->GetValues().bSpaceDust);
    TestTrue(TEXT("off round trip"), SOLSettingsModel::AreEqual(save->GetValues(), values));
    for (int32 index = 0; index < save->OverlayProfiles.Num(); ++index)
    {
        TestFalse(FString::Printf(TEXT("profile %d has no SpaceDust key"), index + 1),
            SettingsRulesHasDustKey(save->OverlayProfiles[index].Overlays));
    }

    // And on again as on
    values.bSpaceDust = true;
    save->SetValues(values);
    TestTrue(TEXT("SetValues stores on"), save->bSpaceDust);
    TestTrue(TEXT("GetValues gives on back"), save->GetValues().bSpaceDust);
    TestTrue(TEXT("on round trip"), SOLSettingsModel::AreEqual(save->GetValues(), values));
    return true;
}

#endif
