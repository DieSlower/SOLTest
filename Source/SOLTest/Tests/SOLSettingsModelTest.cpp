/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSettingsModel.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    constexpr float SETTINGSMODEL_NAN = std::numeric_limits<float>::quiet_NaN();
    constexpr float SETTINGSMODEL_INF = std::numeric_limits<float>::infinity();
    constexpr float SETTINGSMODEL_TOLERANCE = 1.0e-6f;

    //////////////////////////////////////////////////////////////////////////
    // Returns a valid, non-default set of values used as "the player's saved settings"
    SOLSettingsModel::FSettingsValues SettingsModelCustomValues()
    {
        SOLSettingsModel::FSettingsValues values;
        values.MasterVolume = 0.8f;
        values.SfxVolume = 0.6f;
        values.MusicVolume = 0.4f;
        values.MouseSensitivity = 2.0f;
        values.bInvertY = true;
        values.HudScale = 1.25f;
        values.bHudVisible = false;
        values.bDefaultFlightAssist = false;
        values.bVsync = false;
        values.QualityPreset = 1;
        return values;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelDefaultsTest, "SOLTest.SettingsModel.Defaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Default values match the contract and are already sanitized
bool FSOLSettingsModelDefaultsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    const FSettingsValues defaults;
    TestEqual(TEXT("Master volume"), defaults.MasterVolume, 1.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("SFX volume"), defaults.SfxVolume, 1.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Music volume"), defaults.MusicVolume, 0.7f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Mouse sensitivity"), defaults.MouseSensitivity, 1.0f, SETTINGSMODEL_TOLERANCE);
    TestFalse(TEXT("Invert Y"), defaults.bInvertY);
    TestEqual(TEXT("HUD scale"), defaults.HudScale, 1.0f, SETTINGSMODEL_TOLERANCE);
    TestTrue(TEXT("HUD visible"), defaults.bHudVisible);
    TestTrue(TEXT("Default flight assist"), defaults.bDefaultFlightAssist);
    TestTrue(TEXT("VSync"), defaults.bVsync);
    TestEqual(TEXT("Quality preset"), defaults.QualityPreset, 3);
    TestTrue(TEXT("Defaults equal their sanitized form"), AreEqual(defaults, Sanitized(defaults)));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelClampTest, "SOLTest.SettingsModel.Clamp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Out-of-range values clamp to each field's range (volumes [0,1], sensitivity [0.1,5], HUD [0.5,2], quality [0,4])
bool FSOLSettingsModelClampTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    FSettingsValues low;
    low.MasterVolume = -0.5f;
    low.SfxVolume = -1.0f;
    low.MusicVolume = -100.0f;
    low.MouseSensitivity = 0.0f;
    low.HudScale = 0.1f;
    low.QualityPreset = -3;
    const FSettingsValues lowOut = Sanitized(low);
    TestEqual(TEXT("Master low"), lowOut.MasterVolume, 0.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("SFX low"), lowOut.SfxVolume, 0.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Music low"), lowOut.MusicVolume, 0.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Sensitivity low"), lowOut.MouseSensitivity, 0.1f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("HUD scale low"), lowOut.HudScale, 0.5f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Quality low"), lowOut.QualityPreset, 0);

    FSettingsValues high;
    high.MasterVolume = 1.5f;
    high.SfxVolume = 2.0f;
    high.MusicVolume = 10.0f;
    high.MouseSensitivity = 50.0f;
    high.HudScale = 3.0f;
    high.QualityPreset = 9;
    const FSettingsValues highOut = Sanitized(high);
    TestEqual(TEXT("Master high"), highOut.MasterVolume, 1.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("SFX high"), highOut.SfxVolume, 1.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Music high"), highOut.MusicVolume, 1.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Sensitivity high"), highOut.MouseSensitivity, 5.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("HUD scale high"), highOut.HudScale, 2.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Quality high"), highOut.QualityPreset, 4);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelInRangeTest, "SOLTest.SettingsModel.InRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// In-range values, range endpoints and booleans pass through Sanitized unchanged
bool FSOLSettingsModelInRangeTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    const FSettingsValues custom = SettingsModelCustomValues();
    const FSettingsValues out = Sanitized(custom);
    TestEqual(TEXT("Master"), out.MasterVolume, 0.8f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("SFX"), out.SfxVolume, 0.6f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Music"), out.MusicVolume, 0.4f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Sensitivity"), out.MouseSensitivity, 2.0f, SETTINGSMODEL_TOLERANCE);
    TestTrue(TEXT("Invert Y kept"), out.bInvertY);
    TestEqual(TEXT("HUD scale"), out.HudScale, 1.25f, SETTINGSMODEL_TOLERANCE);
    TestFalse(TEXT("HUD hidden kept"), out.bHudVisible);
    TestFalse(TEXT("Flight assist off kept"), out.bDefaultFlightAssist);
    TestFalse(TEXT("VSync off kept"), out.bVsync);
    TestEqual(TEXT("Quality"), out.QualityPreset, 1);

    // Exact range endpoints are legal
    FSettingsValues edges;
    edges.MasterVolume = 0.0f;
    edges.MouseSensitivity = 0.1f;
    edges.HudScale = 2.0f;
    edges.QualityPreset = 0;
    const FSettingsValues edgesOut = Sanitized(edges);
    TestEqual(TEXT("Volume 0 kept"), edgesOut.MasterVolume, 0.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Sensitivity 0.1 kept"), edgesOut.MouseSensitivity, 0.1f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("HUD 2 kept"), edgesOut.HudScale, 2.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Quality 0 kept"), edgesOut.QualityPreset, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelNonFiniteTest, "SOLTest.SettingsModel.NonFinite",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// NaN floats fall back to that field's default; infinities clamp to the range ends
bool FSOLSettingsModelNonFiniteTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    FSettingsValues bad = SettingsModelCustomValues();
    bad.MasterVolume = SETTINGSMODEL_NAN;
    bad.SfxVolume = SETTINGSMODEL_NAN;
    bad.MusicVolume = SETTINGSMODEL_NAN;
    bad.MouseSensitivity = SETTINGSMODEL_NAN;
    bad.HudScale = SETTINGSMODEL_NAN;
    const FSettingsValues out = Sanitized(bad);
    TestEqual(TEXT("NaN master -> 1"), out.MasterVolume, 1.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("NaN SFX -> 1"), out.SfxVolume, 1.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("NaN music -> 0.7"), out.MusicVolume, 0.7f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("NaN sensitivity -> 1"), out.MouseSensitivity, 1.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("NaN HUD scale -> 1"), out.HudScale, 1.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Non-float fields untouched"), out.QualityPreset, 1);
    TestTrue(TEXT("Bool fields untouched"), out.bInvertY);

    FSettingsValues inf;
    inf.MasterVolume = SETTINGSMODEL_INF;
    inf.MouseSensitivity = -SETTINGSMODEL_INF;
    inf.HudScale = SETTINGSMODEL_INF;
    const FSettingsValues infOut = Sanitized(inf);
    TestEqual(TEXT("+inf volume -> 1"), infOut.MasterVolume, 1.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("-inf sensitivity -> 0.1"), infOut.MouseSensitivity, 0.1f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("+inf HUD -> 2"), infOut.HudScale, 2.0f, SETTINGSMODEL_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelSanitizeIdempotentTest, "SOLTest.SettingsModel.SanitizeIdempotent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Sanitizing twice gives the same result as sanitizing once
bool FSOLSettingsModelSanitizeIdempotentTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    FSettingsValues messy;
    messy.MasterVolume = 3.0f;
    messy.MusicVolume = SETTINGSMODEL_NAN;
    messy.MouseSensitivity = -2.0f;
    messy.QualityPreset = 42;
    const FSettingsValues once = Sanitized(messy);
    const FSettingsValues twice = Sanitized(once);
    TestEqual(TEXT("Master"), twice.MasterVolume, once.MasterVolume, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Music"), twice.MusicVolume, once.MusicVolume, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Sensitivity"), twice.MouseSensitivity, once.MouseSensitivity, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Quality"), twice.QualityPreset, once.QualityPreset);
    TestTrue(TEXT("AreEqual agrees"), AreEqual(once, twice));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelAreEqualTest, "SOLTest.SettingsModel.AreEqual",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Equality compares sanitized values, floats within 1e-4, and every bool and int exactly
bool FSOLSettingsModelAreEqualTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    const FSettingsValues base = SettingsModelCustomValues();
    TestTrue(TEXT("Equal to itself"), AreEqual(base, base));

    FSettingsValues nearlySame = base;
    nearlySame.MasterVolume += 0.00005f;
    TestTrue(TEXT("Within 1e-4 is equal"), AreEqual(base, nearlySame));

    FSettingsValues different = base;
    different.MasterVolume += 0.01f;
    TestFalse(TEXT("Float differs"), AreEqual(base, different));

    // Each bool and the int field is significant on its own
    FSettingsValues flipped = base;
    flipped.bInvertY = !flipped.bInvertY;
    TestFalse(TEXT("bInvertY differs"), AreEqual(base, flipped));
    flipped = base;
    flipped.bHudVisible = !flipped.bHudVisible;
    TestFalse(TEXT("bHudVisible differs"), AreEqual(base, flipped));
    flipped = base;
    flipped.bDefaultFlightAssist = !flipped.bDefaultFlightAssist;
    TestFalse(TEXT("bDefaultFlightAssist differs"), AreEqual(base, flipped));
    flipped = base;
    flipped.bVsync = !flipped.bVsync;
    TestFalse(TEXT("bVsync differs"), AreEqual(base, flipped));
    flipped = base;
    flipped.QualityPreset = 2;
    TestFalse(TEXT("QualityPreset differs"), AreEqual(base, flipped));

    // Two out-of-range values that sanitize to the same value compare equal, and NaN equals the default
    FSettingsValues overA;
    overA.SfxVolume = 5.0f;
    FSettingsValues overB;
    overB.SfxVolume = 9.0f;
    TestTrue(TEXT("Both clamp to 1"), AreEqual(overA, overB));
    FSettingsValues nanMusic;
    nanMusic.MusicVolume = SETTINGSMODEL_NAN;
    TestTrue(TEXT("NaN music equals default"), AreEqual(nanMusic, FSettingsValues()));
    TestTrue(TEXT("Symmetric"), AreEqual(FSettingsValues(), nanMusic));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelMakeModelTest, "SOLTest.SettingsModel.MakeModel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A new model holds the sanitized loaded values as both Saved and Pending, and is not dirty
bool FSOLSettingsModelMakeModelTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    FSettingsValues loaded = SettingsModelCustomValues();
    loaded.HudScale = 7.0f;
    const FSettingsModel model = MakeModel(loaded);
    TestEqual(TEXT("Saved sanitized"), model.Saved.HudScale, 2.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Pending sanitized"), model.Pending.HudScale, 2.0f, SETTINGSMODEL_TOLERANCE);
    TestTrue(TEXT("Saved == Pending"), AreEqual(model.Saved, model.Pending));
    TestFalse(TEXT("Fresh model is clean"), IsDirty(model));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelDirtyTest, "SOLTest.SettingsModel.Dirty",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SetPending stores sanitized values; the model is dirty only while Pending differs from Saved
bool FSOLSettingsModelDirtyTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    FSettingsModel model = MakeModel(SettingsModelCustomValues());

    FSettingsValues edited = model.Saved;
    edited.MouseSensitivity = 99.0f;
    SetPending(model, edited);
    TestEqual(TEXT("Pending sanitized"), model.Pending.MouseSensitivity, 5.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Saved untouched"), model.Saved.MouseSensitivity, 2.0f, SETTINGSMODEL_TOLERANCE);
    TestTrue(TEXT("Dirty after a change"), IsDirty(model));

    // Changing a value and then changing it back is not dirty
    SetPending(model, model.Saved);
    TestFalse(TEXT("Clean after setting back"), IsDirty(model));

    // A change below the 1e-4 tolerance is not dirty
    FSettingsValues tiny = model.Saved;
    tiny.MusicVolume += 0.00002f;
    SetPending(model, tiny);
    TestFalse(TEXT("Sub-tolerance change is clean"), IsDirty(model));

    // An out-of-range edit that sanitizes to the saved value is not dirty
    FSettingsModel maxed = MakeModel(FSettingsValues());
    FSettingsValues over = maxed.Saved;
    over.MasterVolume = 4.0f;
    SetPending(maxed, over);
    TestFalse(TEXT("Clamped back to saved is clean"), IsDirty(maxed));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelRevertTest, "SOLTest.SettingsModel.Revert",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Revert discards Pending, returns the saved values and leaves the model clean; reverting twice is harmless
bool FSOLSettingsModelRevertTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    const FSettingsValues saved = SettingsModelCustomValues();
    FSettingsModel model = MakeModel(saved);
    FSettingsValues edited = saved;
    edited.bVsync = true;
    edited.QualityPreset = 4;
    SetPending(model, edited);
    TestTrue(TEXT("Dirty before revert"), IsDirty(model));

    const FSettingsValues reverted = Revert(model);
    TestTrue(TEXT("Returns the saved values"), AreEqual(reverted, saved));
    TestTrue(TEXT("Pending back to saved"), AreEqual(model.Pending, saved));
    TestTrue(TEXT("Saved unchanged"), AreEqual(model.Saved, saved));
    TestFalse(TEXT("Clean after revert"), IsDirty(model));

    const FSettingsValues revertedAgain = Revert(model);
    TestTrue(TEXT("Second revert returns the same"), AreEqual(revertedAgain, saved));
    TestFalse(TEXT("Still clean"), IsDirty(model));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelCommitTest, "SOLTest.SettingsModel.Commit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// CommitIfDirty asks for a write only when something changed, adopts Pending as Saved, and is then a no-op
bool FSOLSettingsModelCommitTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    FSettingsModel model = MakeModel(FSettingsValues());

    // Nothing changed: no write and nothing moves
    TestFalse(TEXT("Clean model needs no write"), CommitIfDirty(model));
    TestTrue(TEXT("Saved still default"), AreEqual(model.Saved, FSettingsValues()));

    // Several edits in one visit are batched into a single commit
    FSettingsValues edited = model.Pending;
    edited.SfxVolume = 0.3f;
    edited.bInvertY = true;
    edited.HudScale = 1.5f;
    SetPending(model, edited);
    TestTrue(TEXT("Dirty model needs a write"), CommitIfDirty(model));
    TestTrue(TEXT("Saved adopts Pending"), AreEqual(model.Saved, edited));
    TestFalse(TEXT("Clean after commit"), IsDirty(model));
    TestFalse(TEXT("Second commit needs no write"), CommitIfDirty(model));

    // After a commit, Revert returns the newly saved values
    TestTrue(TEXT("Revert returns committed values"), AreEqual(Revert(model), edited));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
