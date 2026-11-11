/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSettingsModel.h"
#include "Menu/SOLSettingsRules.h"
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
    TestTrue(TEXT("Space dust follows the constant"), defaults.bSpaceDust == SOL::Settings::SPACE_DUST_DEFAULT);
    TestTrue(TEXT("Space dust on"), defaults.bSpaceDust);
    TestEqual(TEXT("Hologram menu effects"), defaults.HoloMenuEffects, 1.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("HUD glow"), defaults.HudGlow, 1.0f, SETTINGSMODEL_TOLERANCE);
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
    flipped = base;
    flipped.bSpaceDust = !flipped.bSpaceDust;
    TestFalse(TEXT("bSpaceDust differs"), AreEqual(base, flipped));

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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelSpaceDustDefaultTest, "SOLTest.SettingsModel.SpaceDustDefault",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Space dust (issue #34, a global setting) is on by default, Sanitized passes it through, and AreEqual sees it alone
bool FSOLSettingsModelSpaceDustDefaultTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    TestTrue(TEXT("Default constant is on"), SOL::Settings::SPACE_DUST_DEFAULT);
    const FSettingsValues defaults;
    TestTrue(TEXT("Space dust on by default"), defaults.bSpaceDust);
    TestTrue(TEXT("Sanitized keeps true"), Sanitized(defaults).bSpaceDust);

    FSettingsValues off = SettingsModelCustomValues();
    off.bSpaceDust = false;
    const FSettingsValues offOut = Sanitized(off);
    TestFalse(TEXT("Sanitized keeps false"), offOut.bSpaceDust);
    TestEqual(TEXT("Sanitized leaves other fields alone"), offOut.MouseSensitivity, 2.0f, SETTINGSMODEL_TOLERANCE);

    // Only the dust flag differs
    FSettingsValues on = off;
    on.bSpaceDust = true;
    TestFalse(TEXT("bSpaceDust differs"), AreEqual(off, on));
    TestFalse(TEXT("Symmetric"), AreEqual(on, off));
    TestTrue(TEXT("Same flag is equal"), AreEqual(off, off));
    TestTrue(TEXT("Sanitizing twice keeps it"), AreEqual(Sanitized(Sanitized(off)), off));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelSpaceDustDirtyTest, "SOLTest.SettingsModel.SpaceDustDirty",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The dust flag goes through MakeModel, SetPending, IsDirty, Revert and CommitIfDirty like any other Apply / Revert
// setting (SDD 34 D16)
bool FSOLSettingsModelSpaceDustDirtyTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    FSettingsValues loaded = SettingsModelCustomValues();
    loaded.bSpaceDust = false;
    FSettingsModel model = MakeModel(loaded);
    TestFalse(TEXT("MakeModel keeps Saved flag"), model.Saved.bSpaceDust);
    TestFalse(TEXT("MakeModel keeps Pending flag"), model.Pending.bSpaceDust);
    TestFalse(TEXT("Fresh model is clean"), IsDirty(model));

    // A pending change in only the dust flag is dirty, and Revert discards it
    FSettingsValues edited = model.Saved;
    edited.bSpaceDust = true;
    SetPending(model, edited);
    TestTrue(TEXT("Pending takes the flag"), model.Pending.bSpaceDust);
    TestFalse(TEXT("Saved untouched"), model.Saved.bSpaceDust);
    TestTrue(TEXT("Dirty when only the flag differs"), IsDirty(model));
    const FSettingsValues reverted = Revert(model);
    TestFalse(TEXT("Revert returns the saved flag"), reverted.bSpaceDust);
    TestFalse(TEXT("Pending flag reverted"), model.Pending.bSpaceDust);
    TestFalse(TEXT("Clean after revert"), IsDirty(model));

    // Committing the flag alone needs a write and Saved takes it; a second commit needs none
    SetPending(model, edited);
    TestTrue(TEXT("Commit of the flag needs a write"), CommitIfDirty(model));
    TestTrue(TEXT("Saved takes the flag"), model.Saved.bSpaceDust);
    TestFalse(TEXT("Clean after commit"), IsDirty(model));
    TestFalse(TEXT("Second commit needs no write"), CommitIfDirty(model));
    TestTrue(TEXT("Revert after commit returns the committed flag"), Revert(model).bSpaceDust);

    // Applying overlay prefs (O, Alt+O, the cycle key) leaves a pending dust edit alone
    FSettingsModel overlay = MakeModel(SettingsModelCustomValues());
    FSettingsValues pendingDust = overlay.Saved;
    pendingDust.bSpaceDust = false;
    SetPending(overlay, pendingDust);
    SOLOverlay::FOverlayPrefs prefs = overlay.Saved.OverlayPrefs;
    SOLOverlay::ToggleOrbitLines(prefs);
    TestTrue(TEXT("Overlay apply changes the prefs"), SetAppliedOverlayPrefs(overlay, prefs));
    TestFalse(TEXT("Overlay apply keeps the pending dust flag"), overlay.Pending.bSpaceDust);
    TestTrue(TEXT("Overlay apply keeps the saved dust flag"), overlay.Saved.bSpaceDust);
    TestTrue(TEXT("Still dirty from the dust edit"), IsDirty(overlay));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelOverlayOrbitLinesTest, "SOLTest.SettingsModel.OverlayOrbitLines",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The Alt+O orbit-lines switch lives in the overlay prefs (per profile, SDD 34 D18): on by default, applied through
// SetAppliedOverlayPrefs (Saved and Pending together), and a switch-only edit is dirty through the prefs
bool FSOLSettingsModelOverlayOrbitLinesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    const FSettingsValues defaults;
    TestTrue(TEXT("Orbit lines on by default"), SOLOverlay::AreOrbitLinesOn(defaults.OverlayPrefs));

    // Applying the switch off (Alt+O in flight) moves Saved and Pending together and keeps other pending edits
    const FSettingsValues saved = SettingsModelCustomValues();
    FSettingsModel model = MakeModel(saved);
    FSettingsValues edited = saved;
    edited.QualityPreset = 4;
    SetPending(model, edited);
    SOLOverlay::FOverlayPrefs hidden = model.Saved.OverlayPrefs;
    SOLOverlay::SetOrbitLinesOn(hidden, false);
    TestTrue(TEXT("Hiding changes the applied prefs"), SetAppliedOverlayPrefs(model, hidden));
    TestFalse(TEXT("Saved switch off"), SOLOverlay::AreOrbitLinesOn(model.Saved.OverlayPrefs));
    TestFalse(TEXT("Pending switch off"), SOLOverlay::AreOrbitLinesOn(model.Pending.OverlayPrefs));
    TestEqual(TEXT("Pending quality kept"), model.Pending.QualityPreset, 4);
    TestEqual(TEXT("Saved quality kept"), model.Saved.QualityPreset, 1);
    TestFalse(TEXT("Same switch again returns false"), SetAppliedOverlayPrefs(model, hidden));

    // A pending-only switch edit is dirty, Revert discards it, commit adopts it
    FSettingsModel pending = MakeModel(SettingsModelCustomValues());
    FSettingsValues switchOff = pending.Saved;
    SOLOverlay::SetOrbitLinesOn(switchOff.OverlayPrefs, false);
    TestFalse(TEXT("Switch-only values differ"), AreEqual(pending.Saved, switchOff));
    SetPending(pending, switchOff);
    TestTrue(TEXT("Dirty when only the switch differs"), IsDirty(pending));
    TestTrue(TEXT("Revert returns the saved switch"), SOLOverlay::AreOrbitLinesOn(Revert(pending).OverlayPrefs));
    TestFalse(TEXT("Clean after revert"), IsDirty(pending));
    SetPending(pending, switchOff);
    TestTrue(TEXT("Commit of the switch needs a write"), CommitIfDirty(pending));
    TestFalse(TEXT("Saved takes the switch"), SOLOverlay::AreOrbitLinesOn(pending.Saved.OverlayPrefs));
    TestFalse(TEXT("Clean after commit"), IsDirty(pending));
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

    // Applying the orbit-lines switch (now in the overlay prefs) leaves a pending steer-at-start edit alone
    FSettingsModel orbit = MakeModel(SettingsModelCustomValues());
    FSettingsValues pendingSteer = orbit.Saved;
    pendingSteer.bMouseSteerAtStart = true;
    SetPending(orbit, pendingSteer);
    SOLOverlay::FOverlayPrefs orbitOff = orbit.Saved.OverlayPrefs;
    SOLOverlay::SetOrbitLinesOn(orbitOff, false);
    SetAppliedOverlayPrefs(orbit, orbitOff);
    TestTrue(TEXT("Orbit-lines apply keeps pending steer flag"), orbit.Pending.bMouseSteerAtStart);
    TestFalse(TEXT("Orbit-lines apply keeps saved steer flag"), orbit.Saved.bMouseSteerAtStart);
    TestTrue(TEXT("Still dirty from the steer edit"), IsDirty(orbit));
    return true;
}

// Issue #22 overlay prefs in the settings model, written against the issue #22 contract (SDD 22) before the
// production code

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelOverlayPrefsTest, "SOLTest.SettingsModel.OverlayPrefs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The overlay prefs default to the default profile, are sanitized with the values, and AreEqual / IsDirty see a
// single overlay bit
bool FSOLSettingsModelOverlayPrefsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    const FSettingsValues defaults;
    TestTrue(TEXT("Default prefs"), SOLOverlay::AreEqual(defaults.OverlayPrefs, SOLOverlay::MakeDefaultPrefs()));
    TestEqual(TEXT("Default: only profile 1 enabled"), static_cast<int32>(defaults.OverlayPrefs.EnabledProfiles), 0x01);
    TestEqual(TEXT("Default: only profile 1 used"), static_cast<int32>(defaults.OverlayPrefs.UsedProfiles), 0x01);
    TestEqual(TEXT("Default: every master on"), static_cast<int32>(defaults.OverlayPrefs.MasterOnProfiles), 0x1F);
    TestEqual(TEXT("Default: every orbit switch on"), static_cast<int32>(defaults.OverlayPrefs.OrbitLinesProfiles),
        0x1F);
    TestEqual(TEXT("Default: active profile 1"), defaults.OverlayPrefs.ActiveProfile, 0);
    TestEqual(TEXT("Default mask"), defaults.OverlayPrefs.Masks[0], SOLOverlay::DefaultMask());

    // Sanitized cleans the prefs like SOLOverlay::Sanitized
    FSettingsValues messy = SettingsModelCustomValues();
    messy.OverlayPrefs.EnabledProfiles = 0xE0;
    messy.OverlayPrefs.UsedProfiles = 0xFF;
    messy.OverlayPrefs.MasterOnProfiles = 0xFF;
    messy.OverlayPrefs.OrbitLinesProfiles = 0xE3;
    messy.OverlayPrefs.ActiveProfile = -2;
    messy.OverlayPrefs.Masks[0] = 0xFFFFFFFFu;
    const FSettingsValues clean = Sanitized(messy);
    const SOLOverlay::FOverlayPrefs expected = SOLOverlay::Sanitized(messy.OverlayPrefs);
    TestEqual(TEXT("Enabled: garbage stripped, profile 1 forced on"),
        static_cast<int32>(clean.OverlayPrefs.EnabledProfiles), 0x01);
    TestEqual(TEXT("Used: garbage stripped"), static_cast<int32>(clean.OverlayPrefs.UsedProfiles), 0x1F);
    TestEqual(TEXT("Master: garbage stripped"), static_cast<int32>(clean.OverlayPrefs.MasterOnProfiles), 0x1F);
    TestEqual(TEXT("Orbit: garbage stripped"), static_cast<int32>(clean.OverlayPrefs.OrbitLinesProfiles), 0x03);
    TestEqual(TEXT("Active profile clamped"), clean.OverlayPrefs.ActiveProfile, 0);
    TestEqual(TEXT("Invalid bits dropped"), clean.OverlayPrefs.Masks[0], SOLOverlay::AllBitsMask());
    TestTrue(TEXT("Matches SOLOverlay::Sanitized"), SOLOverlay::AreEqual(clean.OverlayPrefs, expected));
    TestEqual(TEXT("Other fields untouched"), clean.MouseSensitivity, 2.0f, SETTINGSMODEL_TOLERANCE);

    // One overlay bit is significant on its own
    const FSettingsValues base = SettingsModelCustomValues();
    FSettingsValues flipped = base;
    SOLOverlay::Toggle(flipped.OverlayPrefs, SOLOverlay::EOverlay::IconAsteroids);
    TestFalse(TEXT("One overlay bit differs"), AreEqual(base, flipped));
    TestFalse(TEXT("Symmetric"), AreEqual(flipped, base));
    FSettingsValues unusedSlot = base;
    unusedSlot.OverlayPrefs.Masks[4] = 0u;
    TestTrue(TEXT("An unused profile slot is ignored"), AreEqual(base, unusedSlot));

    // The model sees an overlay-only edit as dirty; revert discards it, commit adopts it
    FSettingsModel model = MakeModel(base);
    SetPending(model, flipped);
    TestTrue(TEXT("Dirty when only an overlay bit differs"), IsDirty(model));
    Revert(model);
    TestFalse(TEXT("Clean after revert"), IsDirty(model));
    SetPending(model, flipped);
    TestTrue(TEXT("Commit of the overlay bit needs a write"), CommitIfDirty(model));
    TestTrue(TEXT("Saved takes the overlay bit"), SOLOverlay::IsEnabled(model.Saved.OverlayPrefs,
        SOLOverlay::EOverlay::IconAsteroids) == SOLOverlay::IsEnabled(flipped.OverlayPrefs,
        SOLOverlay::EOverlay::IconAsteroids));
    TestFalse(TEXT("Clean after commit"), IsDirty(model));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelSetAppliedOverlayPrefsTest,
    "SOLTest.SettingsModel.SetAppliedOverlayPrefs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SetAppliedOverlayPrefs sets the sanitized prefs in Saved and Pending, keeps every other pending edit, and returns
// true only when the saved prefs changed
bool FSOLSettingsModelSetAppliedOverlayPrefsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    const FSettingsValues saved = SettingsModelCustomValues();
    FSettingsModel model = MakeModel(saved);

    // A pending edit of another field is in flight
    FSettingsValues edited = saved;
    edited.MasterVolume = 0.25f;
    SetPending(model, edited);

    SOLOverlay::FOverlayPrefs prefs = SOLOverlay::MakeDefaultPrefs();
    SOLOverlay::SetEnabled(prefs, SOLOverlay::EOverlay::Darkening, false);
    TestTrue(TEXT("A change returns true"), SetAppliedOverlayPrefs(model, prefs));
    TestTrue(TEXT("Saved prefs set"), SOLOverlay::AreEqual(model.Saved.OverlayPrefs, prefs));
    TestTrue(TEXT("Pending prefs set"), SOLOverlay::AreEqual(model.Pending.OverlayPrefs, prefs));
    TestEqual(TEXT("Pending master volume kept"), model.Pending.MasterVolume, 0.25f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Saved master volume kept"), model.Saved.MasterVolume, 0.8f, SETTINGSMODEL_TOLERANCE);
    TestTrue(TEXT("Still dirty from the other edit"), IsDirty(model));

    // Applying equal prefs again changes nothing
    TestFalse(TEXT("Same prefs return false"), SetAppliedOverlayPrefs(model, prefs));
    TestTrue(TEXT("Saved prefs unchanged"), SOLOverlay::AreEqual(model.Saved.OverlayPrefs, prefs));
    TestEqual(TEXT("Pending master volume still kept"), model.Pending.MasterVolume, 0.25f, SETTINGSMODEL_TOLERANCE);

    // Out-of-range prefs are stored sanitized; ones equal after sanitizing return false
    SOLOverlay::FOverlayPrefs messy = prefs;
    messy.ActiveProfile = 9;
    messy.Masks[0] |= 1u << 31;
    TestFalse(TEXT("Equal after sanitizing returns false"), SetAppliedOverlayPrefs(model, messy));
    TestEqual(TEXT("Saved active sanitized"), model.Saved.OverlayPrefs.ActiveProfile, 0);
    TestEqual(TEXT("Saved mask sanitized"), model.Saved.OverlayPrefs.Masks[0] & ~SOLOverlay::AllBitsMask(), 0u);
    TestEqual(TEXT("Pending active sanitized"), model.Pending.OverlayPrefs.ActiveProfile, 0);

    // On a clean model the prefs alone leave it clean (Saved and Pending move together)
    FSettingsModel clean = MakeModel(FSettingsValues());
    TestTrue(TEXT("Clean model: change returns true"), SetAppliedOverlayPrefs(clean, prefs));
    TestFalse(TEXT("Clean model stays clean"), IsDirty(clean));
    TestFalse(TEXT("Nothing pending to commit"), CommitIfDirty(clean));

    // A pending-only overlay edit is overwritten, and the return value follows Saved only
    FSettingsModel pendingOnly = MakeModel(FSettingsValues());
    FSettingsValues pendingEdit = pendingOnly.Saved;
    SOLOverlay::Toggle(pendingEdit.OverlayPrefs, SOLOverlay::EOverlay::IconRings);
    SetPending(pendingOnly, pendingEdit);
    TestTrue(TEXT("Pending-only edit is dirty"), IsDirty(pendingOnly));
    TestFalse(TEXT("Saved already default returns false"), SetAppliedOverlayPrefs(pendingOnly,
        SOLOverlay::MakeDefaultPrefs()));
    TestTrue(TEXT("Pending overwritten"), SOLOverlay::AreEqual(pendingOnly.Pending.OverlayPrefs,
        SOLOverlay::MakeDefaultPrefs()));
    TestFalse(TEXT("No longer dirty"), IsDirty(pendingOnly));

    // A profile change (enable, cycle, per-profile master and orbit switch) is applied whole to Saved and Pending
    FSettingsModel profiles = MakeModel(FSettingsValues());
    SOLOverlay::FOverlayPrefs cycled = SOLOverlay::MakeDefaultPrefs();
    SOLOverlay::SetProfileEnabled(cycled, 2, true);
    SOLOverlay::CycleActiveProfile(cycled);
    SOLOverlay::SetMasterOn(cycled, false);
    SOLOverlay::SetOrbitLinesOn(cycled, false);
    TestTrue(TEXT("Profile change returns true"), SetAppliedOverlayPrefs(profiles, cycled));
    TestEqual(TEXT("Saved active profile"), profiles.Saved.OverlayPrefs.ActiveProfile, 2);
    TestEqual(TEXT("Pending active profile"), profiles.Pending.OverlayPrefs.ActiveProfile, 2);
    TestTrue(TEXT("Saved profile 3 enabled"), SOLOverlay::IsProfileEnabled(profiles.Saved.OverlayPrefs, 2));
    TestFalse(TEXT("Saved master of profile 3 off"), SOLOverlay::IsMasterOn(profiles.Saved.OverlayPrefs));
    TestFalse(TEXT("Saved orbit switch of profile 3 off"), SOLOverlay::AreOrbitLinesOn(profiles.Saved.OverlayPrefs));
    TestTrue(TEXT("Saved and Pending prefs equal"), SOLOverlay::AreEqual(profiles.Saved.OverlayPrefs,
        profiles.Pending.OverlayPrefs));
    TestFalse(TEXT("Profile change leaves the model clean"), IsDirty(profiles));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelOverlayProfilesEqualTest,
    "SOLTest.SettingsModel.OverlayProfilesEqual",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Settings equality (and so IsDirty and the owed write) covers every profile field of the overlay prefs: the enabled,
// used, master and orbit-lines bits and the active profile (SDD 34 section 4.3)
bool FSOLSettingsModelOverlayProfilesEqualTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;

    // Base: profiles 1 and 2 enabled, profile 1 active
    FSettingsValues base = SettingsModelCustomValues();
    SOLOverlay::SetProfileEnabled(base.OverlayPrefs, 1, true);
    TestTrue(TEXT("Equal to itself"), AreEqual(base, base));

    // Enabled bit
    FSettingsValues changed = base;
    SOLOverlay::SetProfileEnabled(changed.OverlayPrefs, 3, true);
    TestFalse(TEXT("An enabled profile differs"), AreEqual(base, changed));
    TestFalse(TEXT("Symmetric"), AreEqual(changed, base));

    // Used bit only: profile 4 enabled then disabled again is used but disabled, with the same content as an unused
    // slot (a copy of the default profile 1)
    FSettingsValues defaultsBase;
    FSettingsValues usedOnly = defaultsBase;
    SOLOverlay::SetProfileEnabled(usedOnly.OverlayPrefs, 3, true);
    SOLOverlay::SetProfileEnabled(usedOnly.OverlayPrefs, 3, false);
    TestTrue(TEXT("Profile 4 is used"), SOLOverlay::IsProfileUsed(usedOnly.OverlayPrefs, 3));
    TestFalse(TEXT("Profile 4 is disabled"), SOLOverlay::IsProfileEnabled(usedOnly.OverlayPrefs, 3));
    TestFalse(TEXT("A used bit differs"), AreEqual(defaultsBase, usedOnly));

    // Active profile
    changed = base;
    SOLOverlay::SetActiveProfile(changed.OverlayPrefs, 1);
    TestFalse(TEXT("The active profile differs"), AreEqual(base, changed));

    // Master bit of one profile (O)
    changed = base;
    SOLOverlay::SetMasterOn(changed.OverlayPrefs, false);
    TestFalse(TEXT("A master bit differs"), AreEqual(base, changed));

    // Orbit-lines bit of one profile (Alt+O)
    changed = base;
    SOLOverlay::SetOrbitLinesOn(changed.OverlayPrefs, false);
    TestFalse(TEXT("An orbit-lines bit differs"), AreEqual(base, changed));

    // A row of an enabled but not active profile
    changed = base;
    changed.OverlayPrefs.Masks[1] ^= 1u << static_cast<uint32>(SOLOverlay::EOverlay::IconRings);
    TestFalse(TEXT("A row of profile 2 differs"), AreEqual(base, changed));

    // A difference only in an unused slot is ignored
    changed = base;
    changed.OverlayPrefs.Masks[4] = 0u;
    TestTrue(TEXT("An unused slot is ignored"), AreEqual(base, changed));

    // The model sees a profile-only edit as dirty, and Revert discards it
    FSettingsModel model = MakeModel(base);
    FSettingsValues edited = base;
    SOLOverlay::SetActiveProfile(edited.OverlayPrefs, 1);
    SetPending(model, edited);
    TestTrue(TEXT("Dirty when only the active profile differs"), IsDirty(model));
    TestEqual(TEXT("Revert returns the saved active profile"), Revert(model).OverlayPrefs.ActiveProfile, 0);
    TestFalse(TEXT("Clean after revert"), IsDirty(model));
    return true;
}

// Issue #32 Chase Down safety margin in the settings model, written against the SDD 32 section 3.10 contract before
// the production code

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelChaseMarginSanitizeTest,
    "SOLTest.SettingsModel.ChaseSafetyMarginSanitize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The Chase Down safety margin defaults to 0.25, clamps to [0, 0.30], NaN -> the default, infinities -> the ends
bool FSOLSettingsModelChaseMarginSanitizeTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    using namespace SOL::Settings;

    // The constants of the contract
    TestEqual(TEXT("Default constant is 0.25"), CHASE_SAFETY_MARGIN_DEFAULT, 0.25f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Min constant is 0"), CHASE_SAFETY_MARGIN_MIN, 0.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Max constant is 0.30"), CHASE_SAFETY_MARGIN_MAX, 0.30f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Step constant is 0.01"), CHASE_SAFETY_MARGIN_STEP, 0.01f, SETTINGSMODEL_TOLERANCE);

    // The default value is the constant and survives sanitizing
    const FSettingsValues defaults;
    TestEqual(TEXT("Default"), defaults.ChaseSafetyMargin, CHASE_SAFETY_MARGIN_DEFAULT, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Default is 0.25"), defaults.ChaseSafetyMargin, 0.25f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Sanitized default kept"), Sanitized(defaults).ChaseSafetyMargin, CHASE_SAFETY_MARGIN_DEFAULT,
        SETTINGSMODEL_TOLERANCE);

    // In range and the exact ends pass through
    const float inRange[] = { CHASE_SAFETY_MARGIN_MIN, 0.01f, 0.1f, 0.25f, CHASE_SAFETY_MARGIN_MAX };
    for (const float margin : inRange)
    {
        FSettingsValues values = SettingsModelCustomValues();
        values.ChaseSafetyMargin = margin;
        TestEqual(FString::Printf(TEXT("%.2f kept"), margin), Sanitized(values).ChaseSafetyMargin, margin,
            SETTINGSMODEL_TOLERANCE);
    }

    // Out of range clamps
    FSettingsValues values = SettingsModelCustomValues();
    values.ChaseSafetyMargin = -0.1f;
    TestEqual(TEXT("Below -> MIN"), Sanitized(values).ChaseSafetyMargin, CHASE_SAFETY_MARGIN_MIN,
        SETTINGSMODEL_TOLERANCE);
    values.ChaseSafetyMargin = 0.31f;
    TestEqual(TEXT("Just above -> MAX"), Sanitized(values).ChaseSafetyMargin, CHASE_SAFETY_MARGIN_MAX,
        SETTINGSMODEL_TOLERANCE);
    values.ChaseSafetyMargin = 1.0f;
    TestEqual(TEXT("A full meter -> MAX"), Sanitized(values).ChaseSafetyMargin, CHASE_SAFETY_MARGIN_MAX,
        SETTINGSMODEL_TOLERANCE);

    // Non-finite values
    values.ChaseSafetyMargin = SETTINGSMODEL_NAN;
    const FSettingsValues nanOut = Sanitized(values);
    TestEqual(TEXT("NaN -> default"), nanOut.ChaseSafetyMargin, CHASE_SAFETY_MARGIN_DEFAULT, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("NaN -> 0.25"), nanOut.ChaseSafetyMargin, 0.25f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("NaN leaves other fields alone"), nanOut.MouseSensitivity, 2.0f, SETTINGSMODEL_TOLERANCE);
    values.ChaseSafetyMargin = SETTINGSMODEL_INF;
    TestEqual(TEXT("+inf -> MAX"), Sanitized(values).ChaseSafetyMargin, CHASE_SAFETY_MARGIN_MAX,
        SETTINGSMODEL_TOLERANCE);
    values.ChaseSafetyMargin = -SETTINGSMODEL_INF;
    TestEqual(TEXT("-inf -> MIN"), Sanitized(values).ChaseSafetyMargin, CHASE_SAFETY_MARGIN_MIN,
        SETTINGSMODEL_TOLERANCE);

    // Sanitizing a clamped value again changes nothing
    values.ChaseSafetyMargin = 0.9f;
    const FSettingsValues once = Sanitized(values);
    TestEqual(TEXT("Idempotent"), Sanitized(once).ChaseSafetyMargin, once.ChaseSafetyMargin, SETTINGSMODEL_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelChaseMarginDirtyTest, "SOLTest.SettingsModel.ChaseSafetyMarginDirty",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AreEqual sees a 0.01 margin change and ignores 5e-5, so a margin-only edit makes the model dirty
bool FSOLSettingsModelChaseMarginDirtyTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    const FSettingsValues base = SettingsModelCustomValues();

    // One 1% step is a difference on its own
    FSettingsValues changed = base;
    changed.ChaseSafetyMargin = base.ChaseSafetyMargin + 0.01f;
    TestFalse(TEXT("A 0.01 change differs"), AreEqual(base, changed));
    TestFalse(TEXT("Symmetric"), AreEqual(changed, base));

    // Below the tolerance is equal
    FSettingsValues nearlySame = base;
    nearlySame.ChaseSafetyMargin += 0.00005f;
    TestTrue(TEXT("A 5e-5 change is equal"), AreEqual(base, nearlySame));

    // Two values that clamp to the same end, and NaN vs the default, compare equal
    FSettingsValues overA = base;
    overA.ChaseSafetyMargin = 0.5f;
    FSettingsValues overB = base;
    overB.ChaseSafetyMargin = 0.9f;
    TestTrue(TEXT("Both clamp to MAX"), AreEqual(overA, overB));
    FSettingsValues nanMargin;
    nanMargin.ChaseSafetyMargin = SETTINGSMODEL_NAN;
    TestTrue(TEXT("NaN equals the default"), AreEqual(nanMargin, FSettingsValues()));

    // The model sees a margin-only edit as dirty; revert discards it, commit adopts it
    FSettingsValues edited = base;
    edited.ChaseSafetyMargin = 0.10f;
    FSettingsModel model = MakeModel(base);
    SetPending(model, edited);
    TestTrue(TEXT("Dirty when only the margin differs"), IsDirty(model));
    const FSettingsValues reverted = Revert(model);
    TestEqual(TEXT("Revert returns the saved margin"), reverted.ChaseSafetyMargin, base.ChaseSafetyMargin,
        SETTINGSMODEL_TOLERANCE);
    TestFalse(TEXT("Clean after revert"), IsDirty(model));
    SetPending(model, edited);
    TestTrue(TEXT("Commit of the margin needs a write"), CommitIfDirty(model));
    TestEqual(TEXT("Saved takes the margin"), model.Saved.ChaseSafetyMargin, 0.10f, SETTINGSMODEL_TOLERANCE);
    TestFalse(TEXT("Clean after commit"), IsDirty(model));

    // An out-of-range pending margin is stored sanitized
    FSettingsValues over = model.Saved;
    over.ChaseSafetyMargin = 2.0f;
    SetPending(model, over);
    TestEqual(TEXT("Pending sanitized"), model.Pending.ChaseSafetyMargin, SOL::Settings::CHASE_SAFETY_MARGIN_MAX,
        SETTINGSMODEL_TOLERANCE);
    return true;
}

// Issue #42 hologram menu effects strength in the settings model, written against the SDD 42 sections 7 and 9.2
// contract before the production code

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelHoloMenuEffectsSanitizeTest,
    "SOLTest.SettingsModel.HoloMenuEffectsSanitize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The hologram menu effects strength defaults to 1, clamps to [0, 2], NaN -> the default, infinities -> the ends, and
// is sanitized independently of the HUD effects strength
bool FSOLSettingsModelHoloMenuEffectsSanitizeTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    using namespace SOL::Settings;

    // The constants of the contract
    TestEqual(TEXT("Min constant is 0"), HOLO_MENU_EFFECTS_MIN, 0.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Max constant is 2"), HOLO_MENU_EFFECTS_MAX, 2.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Default constant is 1"), HOLO_MENU_EFFECTS_DEFAULT, 1.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Step constant is 0.05"), HOLO_MENU_EFFECTS_STEP, 0.05f, SETTINGSMODEL_TOLERANCE);

    // The default value is the constant and survives sanitizing
    const FSettingsValues defaults;
    TestEqual(TEXT("Default"), defaults.HoloMenuEffects, HOLO_MENU_EFFECTS_DEFAULT, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Default is 1"), defaults.HoloMenuEffects, 1.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Sanitized default kept"), Sanitized(defaults).HoloMenuEffects, HOLO_MENU_EFFECTS_DEFAULT,
        SETTINGSMODEL_TOLERANCE);

    // In range, one step and the exact ends pass through
    const float inRange[] = { HOLO_MENU_EFFECTS_MIN, HOLO_MENU_EFFECTS_STEP, 0.3f, 1.55f, HOLO_MENU_EFFECTS_MAX };
    for (const float strength : inRange)
    {
        FSettingsValues values = SettingsModelCustomValues();
        values.HoloMenuEffects = strength;
        TestEqual(FString::Printf(TEXT("%.2f kept"), strength), Sanitized(values).HoloMenuEffects, strength,
            SETTINGSMODEL_TOLERANCE);
    }

    // Out of range clamps
    FSettingsValues values = SettingsModelCustomValues();
    values.HoloMenuEffects = -0.5f;
    TestEqual(TEXT("Below -> MIN"), Sanitized(values).HoloMenuEffects, HOLO_MENU_EFFECTS_MIN, SETTINGSMODEL_TOLERANCE);
    values.HoloMenuEffects = 2.01f;
    TestEqual(TEXT("Just above -> MAX"), Sanitized(values).HoloMenuEffects, HOLO_MENU_EFFECTS_MAX,
        SETTINGSMODEL_TOLERANCE);
    values.HoloMenuEffects = 7.0f;
    TestEqual(TEXT("Above -> MAX"), Sanitized(values).HoloMenuEffects, HOLO_MENU_EFFECTS_MAX, SETTINGSMODEL_TOLERANCE);

    // Non-finite values
    values.HoloMenuEffects = SETTINGSMODEL_NAN;
    const FSettingsValues nanOut = Sanitized(values);
    TestEqual(TEXT("NaN -> default"), nanOut.HoloMenuEffects, HOLO_MENU_EFFECTS_DEFAULT, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("NaN leaves other fields alone"), nanOut.MouseSensitivity, 2.0f, SETTINGSMODEL_TOLERANCE);
    values.HoloMenuEffects = SETTINGSMODEL_INF;
    TestEqual(TEXT("+inf -> MAX"), Sanitized(values).HoloMenuEffects, HOLO_MENU_EFFECTS_MAX, SETTINGSMODEL_TOLERANCE);
    values.HoloMenuEffects = -SETTINGSMODEL_INF;
    TestEqual(TEXT("-inf -> MIN"), Sanitized(values).HoloMenuEffects, HOLO_MENU_EFFECTS_MIN, SETTINGSMODEL_TOLERANCE);

    // The two effects strengths are separate fields: sanitizing one never moves the other
    FSettingsValues split = SettingsModelCustomValues();
    split.HudEffectsStrength = 0.3f;
    split.HoloMenuEffects = 9.0f;
    const FSettingsValues splitOut = Sanitized(split);
    TestEqual(TEXT("HUD strength kept beside a clamped menu strength"), splitOut.HudEffectsStrength, 0.3f,
        SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Menu strength clamped on its own"), splitOut.HoloMenuEffects, HOLO_MENU_EFFECTS_MAX,
        SETTINGSMODEL_TOLERANCE);
    split.HudEffectsStrength = SETTINGSMODEL_NAN;
    split.HoloMenuEffects = 0.45f;
    const FSettingsValues hudNanOut = Sanitized(split);
    TestEqual(TEXT("A NaN HUD strength does not touch the menu strength"), hudNanOut.HoloMenuEffects, 0.45f,
        SETTINGSMODEL_TOLERANCE);

    // Sanitizing a clamped value again changes nothing
    values.HoloMenuEffects = 5.0f;
    const FSettingsValues once = Sanitized(values);
    TestEqual(TEXT("Idempotent"), Sanitized(once).HoloMenuEffects, once.HoloMenuEffects, SETTINGSMODEL_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelHoloMenuEffectsDirtyTest,
    "SOLTest.SettingsModel.HoloMenuEffectsDirty",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AreEqual compares the hologram menu effects strength within the tolerance, so an edit of only it makes the model
// dirty; revert discards it, commit asks for one write, and an unchanged or set-back value asks for none
bool FSOLSettingsModelHoloMenuEffectsDirtyTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    const FSettingsValues base = SettingsModelCustomValues();

    // One slider step is a difference on its own
    FSettingsValues changed = base;
    changed.HoloMenuEffects = base.HoloMenuEffects + SOL::Settings::HOLO_MENU_EFFECTS_STEP;
    TestFalse(TEXT("A one-step change differs"), AreEqual(base, changed));
    TestFalse(TEXT("Symmetric"), AreEqual(changed, base));

    // Below the tolerance is equal
    FSettingsValues nearlySame = base;
    nearlySame.HoloMenuEffects += SOL::Settings::VALUE_EQUAL_TOLERANCE * 0.5f;
    TestTrue(TEXT("Within the tolerance is equal"), AreEqual(base, nearlySame));

    // Two values that clamp to the same end, and NaN vs the default, compare equal
    FSettingsValues overA = base;
    overA.HoloMenuEffects = 5.0f;
    FSettingsValues overB = base;
    overB.HoloMenuEffects = 9.0f;
    TestTrue(TEXT("Both clamp to MAX"), AreEqual(overA, overB));
    FSettingsValues nanStrength;
    nanStrength.HoloMenuEffects = SETTINGSMODEL_NAN;
    TestTrue(TEXT("NaN equals the default"), AreEqual(nanStrength, FSettingsValues()));

    // The menu and HUD strengths are compared separately: moving one value from one field to the other is a change
    FSettingsValues menuLow = base;
    menuLow.HudEffectsStrength = 1.0f;
    menuLow.HoloMenuEffects = 0.4f;
    FSettingsValues hudLow = base;
    hudLow.HudEffectsStrength = 0.4f;
    hudLow.HoloMenuEffects = 1.0f;
    TestFalse(TEXT("Menu-low differs from HUD-low"), AreEqual(menuLow, hudLow));

    // The model sees a menu-strength-only edit as dirty; revert discards it, commit adopts it with one write
    FSettingsValues edited = base;
    edited.HoloMenuEffects = 0.4f;
    FSettingsModel model = MakeModel(base);
    TestFalse(TEXT("Fresh model is clean"), IsDirty(model));
    TestFalse(TEXT("Fresh model needs no write"), CommitIfDirty(model));
    SetPending(model, edited);
    TestTrue(TEXT("Dirty when only the menu strength differs"), IsDirty(model));
    const FSettingsValues reverted = Revert(model);
    TestEqual(TEXT("Revert returns the saved menu strength"), reverted.HoloMenuEffects, base.HoloMenuEffects,
        SETTINGSMODEL_TOLERANCE);
    TestFalse(TEXT("Clean after revert"), IsDirty(model));
    TestFalse(TEXT("Reverted model needs no write"), CommitIfDirty(model));
    SetPending(model, edited);
    TestTrue(TEXT("Commit of the menu strength needs a write"), CommitIfDirty(model));
    TestEqual(TEXT("Saved takes the menu strength"), model.Saved.HoloMenuEffects, 0.4f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Saved HUD strength untouched"), model.Saved.HudEffectsStrength, base.HudEffectsStrength,
        SETTINGSMODEL_TOLERANCE);
    TestFalse(TEXT("Clean after commit"), IsDirty(model));
    TestFalse(TEXT("Second commit needs no write"), CommitIfDirty(model));

    // Moving the slider away and back within one visit is not dirty and needs no write
    FSettingsValues away = model.Saved;
    away.HoloMenuEffects = 1.8f;
    SetPending(model, away);
    TestTrue(TEXT("Dirty while moved away"), IsDirty(model));
    SetPending(model, model.Saved);
    TestFalse(TEXT("Clean once set back"), IsDirty(model));
    TestFalse(TEXT("Set back needs no write"), CommitIfDirty(model));

    // An out-of-range pending strength is stored sanitized, and one that clamps to the saved value is not dirty
    FSettingsValues over = model.Saved;
    over.HoloMenuEffects = 12.0f;
    SetPending(model, over);
    TestEqual(TEXT("Pending sanitized"), model.Pending.HoloMenuEffects, SOL::Settings::HOLO_MENU_EFFECTS_MAX,
        SETTINGSMODEL_TOLERANCE);
    FSettingsValues atMax = base;
    atMax.HoloMenuEffects = SOL::Settings::HOLO_MENU_EFFECTS_MAX;
    FSettingsModel maxed = MakeModel(atMax);
    FSettingsValues beyond = maxed.Saved;
    beyond.HoloMenuEffects = 4.0f;
    SetPending(maxed, beyond);
    TestFalse(TEXT("Clamped back to saved is clean"), IsDirty(maxed));
    TestFalse(TEXT("Clamped back to saved needs no write"), CommitIfDirty(maxed));
    return true;
}

// Issue #37 HUD glow setting in the settings model, written against the SDD 37 section 5 contract (cases 1-4) before
// the production code

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelHudGlowConstantsTest, "SOLTest.SettingsModel.HudGlowConstants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The HUD glow slider runs 0% to 200% in 5% steps (41 positions) with 100% as the default (D2)
bool FSOLSettingsModelHudGlowConstantsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Settings;
    TestEqual(TEXT("Min constant is 0"), HUD_GLOW_MIN, 0.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Max constant is 2"), HUD_GLOW_MAX, 2.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Default constant is 1"), HUD_GLOW_DEFAULT, 1.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Step constant is 0.05"), HUD_GLOW_STEP, 0.05f, SETTINGSMODEL_TOLERANCE);
    TestTrue(TEXT("Default inside the range"), HUD_GLOW_DEFAULT >= HUD_GLOW_MIN && HUD_GLOW_DEFAULT <= HUD_GLOW_MAX);

    // 41 slider positions, and the default sits on one of them
    const float positions = (HUD_GLOW_MAX - HUD_GLOW_MIN) / HUD_GLOW_STEP + 1.0f;
    TestEqual(TEXT("41 positions"), positions, 41.0f, 1.0e-4f);
    TestEqual(TEXT("41 positions (rounded)"), FMath::RoundToInt(positions), 41);
    const float defaultSteps = (HUD_GLOW_DEFAULT - HUD_GLOW_MIN) / HUD_GLOW_STEP;
    TestEqual(TEXT("Default on a step"), defaultSteps, static_cast<float>(FMath::RoundToInt(defaultSteps)), 1.0e-4f);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelHudGlowSanitizeTest, "SOLTest.SettingsModel.HudGlowSanitize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The HUD glow defaults to 1, clamps to [0, 2], NaN -> the default, infinities -> the ends, and is sanitized
// independently of the HUD and hologram menu effects strengths
bool FSOLSettingsModelHudGlowSanitizeTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    using namespace SOL::Settings;

    // The default value is the constant and survives sanitizing
    const FSettingsValues defaults;
    TestEqual(TEXT("Default"), defaults.HudGlow, HUD_GLOW_DEFAULT, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Default is 1"), defaults.HudGlow, 1.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Sanitized default kept"), Sanitized(defaults).HudGlow, HUD_GLOW_DEFAULT, SETTINGSMODEL_TOLERANCE);

    // In range, one step and the exact ends pass through
    const float inRange[] = { HUD_GLOW_MIN, HUD_GLOW_STEP, 0.35f, 1.55f, HUD_GLOW_MAX };
    for (const float glow : inRange)
    {
        FSettingsValues values = SettingsModelCustomValues();
        values.HudGlow = glow;
        TestEqual(FString::Printf(TEXT("%.2f kept"), glow), Sanitized(values).HudGlow, glow, SETTINGSMODEL_TOLERANCE);
    }

    // Out of range clamps
    FSettingsValues values = SettingsModelCustomValues();
    values.HudGlow = -0.5f;
    TestEqual(TEXT("Below -> MIN"), Sanitized(values).HudGlow, HUD_GLOW_MIN, SETTINGSMODEL_TOLERANCE);
    values.HudGlow = 2.01f;
    TestEqual(TEXT("Just above -> MAX"), Sanitized(values).HudGlow, HUD_GLOW_MAX, SETTINGSMODEL_TOLERANCE);
    values.HudGlow = 7.0f;
    TestEqual(TEXT("Above -> MAX"), Sanitized(values).HudGlow, HUD_GLOW_MAX, SETTINGSMODEL_TOLERANCE);

    // Non-finite values
    values.HudGlow = SETTINGSMODEL_NAN;
    const FSettingsValues nanOut = Sanitized(values);
    TestEqual(TEXT("NaN -> default"), nanOut.HudGlow, HUD_GLOW_DEFAULT, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("NaN -> 1"), nanOut.HudGlow, 1.0f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("NaN leaves other fields alone"), nanOut.MouseSensitivity, 2.0f, SETTINGSMODEL_TOLERANCE);
    values.HudGlow = SETTINGSMODEL_INF;
    TestEqual(TEXT("+inf -> MAX"), Sanitized(values).HudGlow, HUD_GLOW_MAX, SETTINGSMODEL_TOLERANCE);
    values.HudGlow = -SETTINGSMODEL_INF;
    TestEqual(TEXT("-inf -> MIN"), Sanitized(values).HudGlow, HUD_GLOW_MIN, SETTINGSMODEL_TOLERANCE);

    // The glow clamps on its own: the effects strengths beside it are never moved, and their sanitizing never moves it
    FSettingsValues split = SettingsModelCustomValues();
    split.HudEffectsStrength = 0.3f;
    split.HoloMenuEffects = 1.7f;
    split.HudGlow = 9.0f;
    const FSettingsValues splitOut = Sanitized(split);
    TestEqual(TEXT("HUD effects kept beside a clamped glow"), splitOut.HudEffectsStrength, 0.3f,
        SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Menu effects kept beside a clamped glow"), splitOut.HoloMenuEffects, 1.7f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Glow clamped on its own"), splitOut.HudGlow, HUD_GLOW_MAX, SETTINGSMODEL_TOLERANCE);
    split.HudEffectsStrength = SETTINGSMODEL_NAN;
    split.HoloMenuEffects = 9.0f;
    split.HudGlow = 0.35f;
    const FSettingsValues effectsBadOut = Sanitized(split);
    TestEqual(TEXT("Bad effects strengths do not touch the glow"), effectsBadOut.HudGlow, 0.35f,
        SETTINGSMODEL_TOLERANCE);

    // Sanitizing a clamped value again changes nothing
    values.HudGlow = 5.0f;
    const FSettingsValues once = Sanitized(values);
    TestEqual(TEXT("Idempotent"), Sanitized(once).HudGlow, once.HudGlow, SETTINGSMODEL_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelHudGlowDirtyTest, "SOLTest.SettingsModel.HudGlowDirty",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AreEqual compares the HUD glow within VALUE_EQUAL_TOLERANCE, so a glow-only edit makes the model dirty; revert
// returns the saved glow, commit asks for one write, and SetPending stores it sanitized
bool FSOLSettingsModelHudGlowDirtyTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsModel;
    using namespace SOL::Settings;
    const FSettingsValues base = SettingsModelCustomValues();

    // A difference beyond the tolerance is a change; within half of it is not
    FSettingsValues changed = base;
    changed.HudGlow = base.HudGlow + VALUE_EQUAL_TOLERANCE * 2.0f;
    TestFalse(TEXT("Beyond the tolerance differs"), AreEqual(base, changed));
    TestFalse(TEXT("Symmetric"), AreEqual(changed, base));
    FSettingsValues step = base;
    step.HudGlow = base.HudGlow - HUD_GLOW_STEP;
    TestFalse(TEXT("A one-step change differs"), AreEqual(base, step));
    FSettingsValues nearlySame = base;
    nearlySame.HudGlow += VALUE_EQUAL_TOLERANCE * 0.5f;
    TestTrue(TEXT("Within half the tolerance is equal"), AreEqual(base, nearlySame));
    FSettingsModel tolerance = MakeModel(base);
    SetPending(tolerance, changed);
    TestTrue(TEXT("Model dirty beyond the tolerance"), IsDirty(tolerance));
    SetPending(tolerance, nearlySame);
    TestFalse(TEXT("Model clean within half the tolerance"), IsDirty(tolerance));

    // Two values that clamp to the same end, and NaN vs the default, compare equal
    FSettingsValues overA = base;
    overA.HudGlow = 5.0f;
    FSettingsValues overB = base;
    overB.HudGlow = 9.0f;
    TestTrue(TEXT("Both clamp to MAX"), AreEqual(overA, overB));
    FSettingsValues underA = base;
    underA.HudGlow = -1.0f;
    FSettingsValues underB = base;
    underB.HudGlow = -SETTINGSMODEL_INF;
    TestTrue(TEXT("Both clamp to MIN"), AreEqual(underA, underB));
    FSettingsValues nanGlow;
    nanGlow.HudGlow = SETTINGSMODEL_NAN;
    TestTrue(TEXT("NaN equals the default"), AreEqual(nanGlow, FSettingsValues()));

    // The glow and the HUD effects strength are compared separately
    FSettingsValues glowLow = base;
    glowLow.HudEffectsStrength = 1.0f;
    glowLow.HudGlow = 0.4f;
    FSettingsValues effectsLow = base;
    effectsLow.HudEffectsStrength = 0.4f;
    effectsLow.HudGlow = 1.0f;
    TestFalse(TEXT("Glow-low differs from effects-low"), AreEqual(glowLow, effectsLow));

    // The model sees a glow-only edit as dirty; revert discards it, commit adopts it with one write
    FSettingsValues edited = base;
    edited.HudGlow = 0.35f;
    FSettingsModel model = MakeModel(base);
    TestFalse(TEXT("Fresh model is clean"), IsDirty(model));
    TestFalse(TEXT("Fresh model needs no write"), CommitIfDirty(model));
    SetPending(model, edited);
    TestTrue(TEXT("Dirty when only the glow differs"), IsDirty(model));
    TestEqual(TEXT("Saved glow untouched by SetPending"), model.Saved.HudGlow, base.HudGlow, SETTINGSMODEL_TOLERANCE);
    const FSettingsValues reverted = Revert(model);
    TestEqual(TEXT("Revert returns the saved glow"), reverted.HudGlow, base.HudGlow, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Pending glow reverted"), model.Pending.HudGlow, base.HudGlow, SETTINGSMODEL_TOLERANCE);
    TestFalse(TEXT("Clean after revert"), IsDirty(model));
    TestFalse(TEXT("Reverted model needs no write"), CommitIfDirty(model));
    SetPending(model, edited);
    TestTrue(TEXT("Commit of the glow needs a write"), CommitIfDirty(model));
    TestEqual(TEXT("Saved takes the glow"), model.Saved.HudGlow, 0.35f, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("Saved HUD effects untouched"), model.Saved.HudEffectsStrength, base.HudEffectsStrength,
        SETTINGSMODEL_TOLERANCE);
    TestFalse(TEXT("Clean after commit"), IsDirty(model));
    TestFalse(TEXT("Second commit needs no write"), CommitIfDirty(model));
    TestEqual(TEXT("Revert after commit returns the committed glow"), Revert(model).HudGlow, 0.35f,
        SETTINGSMODEL_TOLERANCE);

    // Moving the slider away and back within one visit is not dirty and needs no write
    FSettingsValues away = model.Saved;
    away.HudGlow = 1.8f;
    SetPending(model, away);
    TestTrue(TEXT("Dirty while moved away"), IsDirty(model));
    SetPending(model, model.Saved);
    TestFalse(TEXT("Clean once set back"), IsDirty(model));
    TestFalse(TEXT("Set back needs no write"), CommitIfDirty(model));

    // An out-of-range pending glow is stored sanitized, and one that clamps to the saved value is not dirty
    FSettingsValues over = model.Saved;
    over.HudGlow = 12.0f;
    SetPending(model, over);
    TestEqual(TEXT("Pending sanitized (12 -> 2)"), model.Pending.HudGlow, HUD_GLOW_MAX, SETTINGSMODEL_TOLERANCE);
    FSettingsValues atMax = base;
    atMax.HudGlow = HUD_GLOW_MAX;
    FSettingsModel maxed = MakeModel(atMax);
    FSettingsValues beyond = maxed.Saved;
    beyond.HudGlow = 4.0f;
    SetPending(maxed, beyond);
    TestFalse(TEXT("Clamped back to saved is clean"), IsDirty(maxed));
    TestFalse(TEXT("Clamped back to saved needs no write"), CommitIfDirty(maxed));

    // MakeModel sanitizes a loaded glow (a corrupt file holding NaN or 9)
    FSettingsValues loadedNan = base;
    loadedNan.HudGlow = SETTINGSMODEL_NAN;
    TestEqual(TEXT("MakeModel: NaN -> 1"), MakeModel(loadedNan).Saved.HudGlow, HUD_GLOW_DEFAULT,
        SETTINGSMODEL_TOLERANCE);
    FSettingsValues loadedHigh = base;
    loadedHigh.HudGlow = 9.0f;
    TestEqual(TEXT("MakeModel: 9 -> 2"), MakeModel(loadedHigh).Pending.HudGlow, HUD_GLOW_MAX, SETTINGSMODEL_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsModelHudGlowSliderStepsTest, "SOLTest.SettingsModel.HudGlowSliderSteps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The HUD glow slider snaps to 5% steps inside [0%, 200%] and reads as a whole percentage (D2, D6)
bool FSOLSettingsModelHudGlowSliderStepsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLSettingsRules;
    using namespace SOL::Settings;
    TestEqual(TEXT("0.52 snaps to 0.5"), SnapToStep(0.52f, HUD_GLOW_STEP, HUD_GLOW_MIN, HUD_GLOW_MAX), 0.5f,
        SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("1.976 snaps to 2"), SnapToStep(1.976f, HUD_GLOW_STEP, HUD_GLOW_MIN, HUD_GLOW_MAX), 2.0f,
        SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("-1 clamps to 0"), SnapToStep(-1.0f, HUD_GLOW_STEP, HUD_GLOW_MIN, HUD_GLOW_MAX), 0.0f,
        SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("5 clamps to 2"), SnapToStep(5.0f, HUD_GLOW_STEP, HUD_GLOW_MIN, HUD_GLOW_MAX), 2.0f,
        SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("the default is a step"), SnapToStep(HUD_GLOW_DEFAULT, HUD_GLOW_STEP, HUD_GLOW_MIN, HUD_GLOW_MAX),
        HUD_GLOW_DEFAULT, SETTINGSMODEL_TOLERANCE);
    TestEqual(TEXT("0%"), FormatPercent(0.0f), FString(TEXT("0%")));
    TestEqual(TEXT("5%"), FormatPercent(0.05f), FString(TEXT("5%")));
    TestEqual(TEXT("100%"), FormatPercent(1.0f), FString(TEXT("100%")));
    TestEqual(TEXT("200%"), FormatPercent(2.0f), FString(TEXT("200%")));
    TestEqual(TEXT("the default reads 100%"), FormatPercent(HUD_GLOW_DEFAULT), FString(TEXT("100%")));
    TestEqual(TEXT("the max reads 200%"), FormatPercent(HUD_GLOW_MAX), FString(TEXT("200%")));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
