/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSettingsModel.h"
#include "SOLConstants.h"

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

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelOrbitLinesDefaultTest, "SOLTest.SettingsModel.OrbitLinesDefault",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Orbit lines are shown by default, Sanitized passes the flag through, and AreEqual sees it on its own
bool FSOLSettingsModelOrbitLinesDefaultTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    const FSettingsValues defaults;
    TestTrue(TEXT("Orbit lines shown by default"), defaults.bShowOrbitLines);
    TestTrue(TEXT("Sanitized keeps true"), Sanitized(defaults).bShowOrbitLines);

    FSettingsValues hidden = SettingsModelCustomValues();
    hidden.bShowOrbitLines = false;
    TestFalse(TEXT("Sanitized keeps false"), Sanitized(hidden).bShowOrbitLines);

    // Only the orbit-lines flag differs
    FSettingsValues shown = hidden;
    shown.bShowOrbitLines = true;
    TestFalse(TEXT("bShowOrbitLines differs"), AreEqual(hidden, shown));
    TestFalse(TEXT("Symmetric"), AreEqual(shown, hidden));
    TestTrue(TEXT("Same flag is equal"), AreEqual(hidden, hidden));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelOrbitLinesDirtyTest, "SOLTest.SettingsModel.OrbitLinesDirty",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The orbit-lines flag goes through MakeModel, SetPending, IsDirty, Revert and CommitIfDirty like any other field
bool FSOLSettingsModelOrbitLinesDirtyTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    FSettingsValues loaded = SettingsModelCustomValues();
    loaded.bShowOrbitLines = false;
    FSettingsModel model = MakeModel(loaded);
    TestFalse(TEXT("MakeModel keeps Saved flag"), model.Saved.bShowOrbitLines);
    TestFalse(TEXT("MakeModel keeps Pending flag"), model.Pending.bShowOrbitLines);
    TestFalse(TEXT("Fresh model is clean"), IsDirty(model));

    // A pending change in only the orbit-lines flag is dirty, and Revert discards it
    FSettingsValues edited = model.Saved;
    edited.bShowOrbitLines = true;
    SetPending(model, edited);
    TestTrue(TEXT("Pending takes the flag"), model.Pending.bShowOrbitLines);
    TestFalse(TEXT("Saved untouched"), model.Saved.bShowOrbitLines);
    TestTrue(TEXT("Dirty when only the flag differs"), IsDirty(model));
    const FSettingsValues reverted = Revert(model);
    TestFalse(TEXT("Revert returns the saved flag"), reverted.bShowOrbitLines);
    TestFalse(TEXT("Pending flag reverted"), model.Pending.bShowOrbitLines);
    TestFalse(TEXT("Clean after revert"), IsDirty(model));

    // Committing the flag alone needs a write and Saved takes it
    SetPending(model, edited);
    TestTrue(TEXT("Commit of the flag needs a write"), CommitIfDirty(model));
    TestTrue(TEXT("Saved takes the flag"), model.Saved.bShowOrbitLines);
    TestFalse(TEXT("Clean after commit"), IsDirty(model));
    TestFalse(TEXT("Second commit needs no write"), CommitIfDirty(model));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelSetAppliedOrbitLinesTest,
    "SOLTest.SettingsModel.SetAppliedOrbitLines",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SetAppliedShowOrbitLines sets the flag in both Saved and Pending, keeps every other field (pending edits included),
// and returns true only when the saved flag changed
bool FSOLSettingsModelSetAppliedOrbitLinesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    const FSettingsValues saved = SettingsModelCustomValues();
    FSettingsModel model = MakeModel(saved);

    // A pending edit of another field is in flight
    FSettingsValues edited = saved;
    edited.MasterVolume = 0.25f;
    edited.QualityPreset = 4;
    SetPending(model, edited);

    TestTrue(TEXT("Hiding changes the saved flag"), SetAppliedShowOrbitLines(model, false));
    TestFalse(TEXT("Saved flag hidden"), model.Saved.bShowOrbitLines);
    TestFalse(TEXT("Pending flag hidden"), model.Pending.bShowOrbitLines);

    // Every other field of Saved and Pending is untouched
    FSettingsValues savedExpected = saved;
    savedExpected.bShowOrbitLines = false;
    TestTrue(TEXT("Saved otherwise unchanged"), AreEqual(model.Saved, savedExpected));
    FSettingsValues pendingExpected = edited;
    pendingExpected.bShowOrbitLines = false;
    TestTrue(TEXT("Pending edits kept"), AreEqual(model.Pending, pendingExpected));
    TestEqual(TEXT("Pending master volume kept"), model.Pending.MasterVolume, 0.25f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Pending quality kept"), model.Pending.QualityPreset, 4);
    TestEqual(TEXT("Saved master volume kept"), model.Saved.MasterVolume, 0.8f, SETTINGSMODEL_TOLERANCE);
    TestTrue(TEXT("Still dirty from the other edits"), IsDirty(model));

    // Applying the same value again changes nothing
    TestFalse(TEXT("Same value returns false"), SetAppliedShowOrbitLines(model, false));
    TestFalse(TEXT("Saved flag still hidden"), model.Saved.bShowOrbitLines);
    TestFalse(TEXT("Pending flag still hidden"), model.Pending.bShowOrbitLines);

    // Showing again changes it back
    TestTrue(TEXT("Showing changes the saved flag"), SetAppliedShowOrbitLines(model, true));
    TestTrue(TEXT("Saved flag shown"), model.Saved.bShowOrbitLines);
    TestTrue(TEXT("Pending flag shown"), model.Pending.bShowOrbitLines);

    // Applied on a clean model, the flag alone leaves it clean (Saved and Pending move together)
    FSettingsModel clean = MakeModel(FSettingsValues());
    TestTrue(TEXT("Clean model: change returns true"), SetAppliedShowOrbitLines(clean, false));
    TestFalse(TEXT("Clean model stays clean"), IsDirty(clean));
    TestFalse(TEXT("Nothing pending to commit"), CommitIfDirty(clean));

    // A pending-only flag change is overwritten in Pending, and the return value follows Saved only
    FSettingsModel pendingOnly = MakeModel(FSettingsValues());
    FSettingsValues pendingHidden = pendingOnly.Saved;
    pendingHidden.bShowOrbitLines = false;
    SetPending(pendingOnly, pendingHidden);
    TestFalse(TEXT("Saved already true returns false"), SetAppliedShowOrbitLines(pendingOnly, true));
    TestTrue(TEXT("Pending overwritten to true"), pendingOnly.Pending.bShowOrbitLines);
    TestFalse(TEXT("No longer dirty"), IsDirty(pendingOnly));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelHudEffectsSanitizeTest, "SOLTest.SettingsModel.HudEffectsSanitize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The HUD effects strength defaults to HUD_EFFECTS_DEFAULT, clamps to [MIN, MAX], NaN -> default, infinities -> ends
bool FSOLSettingsModelHudEffectsSanitizeTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    using namespace SOL::Settings;
    const FSettingsValues defaults;
    TestEqual(TEXT("Default"), defaults.HudEffectsStrength, HUD_EFFECTS_DEFAULT, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Default is 1"), defaults.HudEffectsStrength, 1.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Sanitized default kept"), Sanitized(defaults).HudEffectsStrength, HUD_EFFECTS_DEFAULT,
        SETTINGSMODEL_TOLERANCE);

    // In range and the exact ends pass through
    const float inRange[] = { HUD_EFFECTS_MIN, 0.3f, 1.55f, HUD_EFFECTS_MAX };
    for (const float strength : inRange)
    {
        FSettingsValues values = SettingsModelCustomValues();
        values.HudEffectsStrength = strength;
        TestEqual(FString::Printf(TEXT("%.2f kept"), strength), Sanitized(values).HudEffectsStrength, strength,
            SETTINGSMODEL_TOLERANCE);
    }

    // Out of range clamps
    FSettingsValues values = SettingsModelCustomValues();
    values.HudEffectsStrength = -0.5f;
    TestEqual(TEXT("Below -> MIN"), Sanitized(values).HudEffectsStrength, HUD_EFFECTS_MIN, SETTINGSMODEL_TOLERANCE);
    values.HudEffectsStrength = 7.0f;
    TestEqual(TEXT("Above -> MAX"), Sanitized(values).HudEffectsStrength, HUD_EFFECTS_MAX, SETTINGSMODEL_TOLERANCE);

    // Non-finite values
    values.HudEffectsStrength = SETTINGSMODEL_NAN;
    const FSettingsValues nanOut = Sanitized(values);
    TestEqual(TEXT("NaN -> default"), nanOut.HudEffectsStrength, HUD_EFFECTS_DEFAULT, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("NaN leaves other fields alone"), nanOut.MouseSensitivity, 2.0f, SETTINGSMODEL_TOLERANCE);
    values.HudEffectsStrength = SETTINGSMODEL_INF;
    TestEqual(TEXT("+inf -> MAX"), Sanitized(values).HudEffectsStrength, HUD_EFFECTS_MAX, SETTINGSMODEL_TOLERANCE);
    values.HudEffectsStrength = -SETTINGSMODEL_INF;
    TestEqual(TEXT("-inf -> MIN"), Sanitized(values).HudEffectsStrength, HUD_EFFECTS_MIN, SETTINGSMODEL_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelHudEffectsDirtyTest, "SOLTest.SettingsModel.HudEffectsDirty",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AreEqual compares the HUD effects strength within the tolerance, so changing only it makes the model dirty
bool FSOLSettingsModelHudEffectsDirtyTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    const FSettingsValues base = SettingsModelCustomValues();

    FSettingsValues changed = base;
    changed.HudEffectsStrength = 0.4f;
    TestFalse(TEXT("Strength differs"), AreEqual(base, changed));
    TestFalse(TEXT("Symmetric"), AreEqual(changed, base));

    FSettingsValues nearlySame = base;
    nearlySame.HudEffectsStrength += SOL::Settings::VALUE_EQUAL_TOLERANCE * 0.5f;
    TestTrue(TEXT("Within the tolerance is equal"), AreEqual(base, nearlySame));

    // Two values that clamp to the same end, and NaN vs the default, compare equal
    FSettingsValues overA = base;
    overA.HudEffectsStrength = 5.0f;
    FSettingsValues overB = base;
    overB.HudEffectsStrength = 9.0f;
    TestTrue(TEXT("Both clamp to MAX"), AreEqual(overA, overB));
    FSettingsValues nanStrength;
    nanStrength.HudEffectsStrength = SETTINGSMODEL_NAN;
    TestTrue(TEXT("NaN equals the default"), AreEqual(nanStrength, FSettingsValues()));

    // The model sees a strength-only edit as dirty; commit adopts it, revert discards it
    FSettingsModel model = MakeModel(base);
    SetPending(model, changed);
    TestTrue(TEXT("Dirty when only the strength differs"), IsDirty(model));
    const FSettingsValues reverted = Revert(model);
    TestEqual(TEXT("Revert returns the saved strength"), reverted.HudEffectsStrength, base.HudEffectsStrength,
        SETTINGSMODEL_TOLERANCE);
    TestFalse(TEXT("Clean after revert"), IsDirty(model));
    SetPending(model, changed);
    TestTrue(TEXT("Commit of the strength needs a write"), CommitIfDirty(model));
    TestEqual(TEXT("Saved takes the strength"), model.Saved.HudEffectsStrength, 0.4f, SETTINGSMODEL_TOLERANCE);
    TestFalse(TEXT("Clean after commit"), IsDirty(model));

    // An out-of-range pending strength is stored sanitized
    FSettingsValues over = model.Saved;
    over.HudEffectsStrength = 12.0f;
    SetPending(model, over);
    TestEqual(TEXT("Pending sanitized"), model.Pending.HudEffectsStrength, SOL::Settings::HUD_EFFECTS_MAX,
        SETTINGSMODEL_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelMouseSteerDefaultTest,
    "SOLTest.SettingsModel.MouseSteerAtStartDefault",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Mouse steering at start defaults to SHIP_STEER_TOGGLE_START_ON (off); Sanitized and AreEqual treat it as a bool
bool FSOLSettingsModelMouseSteerDefaultTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    const FSettingsValues defaults;
    TestFalse(TEXT("Start-on constant is off"), SOL::SHIP_STEER_TOGGLE_START_ON);
    TestTrue(TEXT("Default follows the constant"), defaults.bMouseSteerAtStart == SOL::SHIP_STEER_TOGGLE_START_ON);
    TestFalse(TEXT("Default is off"), defaults.bMouseSteerAtStart);

    FSettingsValues on = SettingsModelCustomValues();
    on.bMouseSteerAtStart = true;
    TestTrue(TEXT("Sanitized keeps true"), Sanitized(on).bMouseSteerAtStart);
    FSettingsValues off = SettingsModelCustomValues();
    off.bMouseSteerAtStart = false;
    TestFalse(TEXT("Sanitized keeps false"), Sanitized(off).bMouseSteerAtStart);

    // Only the steer-at-start flag differs
    TestFalse(TEXT("bMouseSteerAtStart differs"), AreEqual(off, on));
    TestFalse(TEXT("Symmetric"), AreEqual(on, off));
    TestTrue(TEXT("Same flag is equal"), AreEqual(on, on));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelMouseSteerDirtyTest,
    "SOLTest.SettingsModel.MouseSteerAtStartDirty",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The steer-at-start flag goes through MakeModel, SetPending, IsDirty, Revert and CommitIfDirty like any other field
bool FSOLSettingsModelMouseSteerDirtyTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    FSettingsModel model = MakeModel(SettingsModelCustomValues());
    TestFalse(TEXT("MakeModel keeps Saved flag"), model.Saved.bMouseSteerAtStart);
    TestFalse(TEXT("MakeModel keeps Pending flag"), model.Pending.bMouseSteerAtStart);
    TestFalse(TEXT("Fresh model is clean"), IsDirty(model));

    // A pending change in only this flag is dirty, and Revert discards it
    FSettingsValues edited = model.Saved;
    edited.bMouseSteerAtStart = true;
    SetPending(model, edited);
    TestTrue(TEXT("Pending takes the flag"), model.Pending.bMouseSteerAtStart);
    TestFalse(TEXT("Saved untouched"), model.Saved.bMouseSteerAtStart);
    TestTrue(TEXT("Dirty when only the flag differs"), IsDirty(model));
    const FSettingsValues reverted = Revert(model);
    TestFalse(TEXT("Revert returns the saved flag"), reverted.bMouseSteerAtStart);
    TestFalse(TEXT("Pending flag reverted"), model.Pending.bMouseSteerAtStart);
    TestFalse(TEXT("Clean after revert"), IsDirty(model));

    // Committing the flag alone needs a write and Saved takes it; a second commit needs none
    SetPending(model, edited);
    TestTrue(TEXT("Commit of the flag needs a write"), CommitIfDirty(model));
    TestTrue(TEXT("Saved takes the flag"), model.Saved.bMouseSteerAtStart);
    TestFalse(TEXT("Clean after commit"), IsDirty(model));
    TestFalse(TEXT("Second commit needs no write"), CommitIfDirty(model));

    // Applying the orbit-lines flag leaves a pending steer-at-start edit alone
    FSettingsModel orbit = MakeModel(SettingsModelCustomValues());
    FSettingsValues pendingSteer = orbit.Saved;
    pendingSteer.bMouseSteerAtStart = true;
    SetPending(orbit, pendingSteer);
    SetAppliedShowOrbitLines(orbit, false);
    TestTrue(TEXT("Orbit-lines apply keeps pending steer flag"), orbit.Pending.bMouseSteerAtStart);
    TestFalse(TEXT("Orbit-lines apply keeps saved steer flag"), orbit.Saved.bMouseSteerAtStart);
    TestTrue(TEXT("Still dirty from the steer edit"), IsDirty(orbit));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
