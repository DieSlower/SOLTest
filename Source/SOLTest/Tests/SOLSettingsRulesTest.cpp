/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSettingsRules.h"
#include "Menu/SOLSettingsSave.h"

#include "GenericPlatform/GenericWindow.h"
#include "Misc/AutomationTest.h"

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

#endif
