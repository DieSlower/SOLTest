/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLEngineSettingsSnapshot.h"

#include "GameFramework/GameUserSettings.h"
#include "GenericPlatform/GenericWindow.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    const FIntPoint SETTINGSSNAPSHOT_TEST_CONFIRMED(1920, 1080);   // The player's confirmed resolution
    const FIntPoint SETTINGSSNAPSHOT_TEST_CURRENT(1280, 720);      // ... and the current one
    const FIntPoint SETTINGSSNAPSHOT_TEST_SMOKE(1000, 800);        // A smoke's window size
    constexpr float SETTINGSSNAPSHOT_TEST_FRAME_LIMIT = 120.0f;
    constexpr int32 SETTINGSSNAPSHOT_TEST_CHANGED = 8;              // The elements the smoke below changes

    //////////////////////////////////////////////////////////////////////////
    // Returns a transient settings object holding a player's values: a confirmed mode that differs from the current
    UGameUserSettings* SettingsSnapshotTestPlayer()
    {
        UGameUserSettings* settings = NewObject<UGameUserSettings>(GetTransientPackage());
        settings->SetScreenResolution(SETTINGSSNAPSHOT_TEST_CONFIRMED);
        settings->SetFullscreenMode(EWindowMode::Fullscreen);
        settings->ConfirmVideoMode();
        settings->SetScreenResolution(SETTINGSSNAPSHOT_TEST_CURRENT);
        settings->SetFullscreenMode(EWindowMode::WindowedFullscreen);
        settings->SetVSyncEnabled(true);
        settings->SetFrameRateLimit(SETTINGSSNAPSHOT_TEST_FRAME_LIMIT);
        return settings;
    }

    //////////////////////////////////////////////////////////////////////////
    // Changes the settings as a smoke run does: a windowed resize the engine confirms, vsync and the frame-rate limit
    void SettingsSnapshotTestSmoke(UGameUserSettings& settings)
    {
        settings.SetFullscreenMode(EWindowMode::Windowed);
        settings.SetScreenResolution(SETTINGSSNAPSHOT_TEST_SMOKE);
        settings.ConfirmVideoMode();
        settings.SetVSyncEnabled(false);
        settings.SetFrameRateLimit(0.0f);
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsSnapshotRoundTripTest, "SOLTest.SettingsSnapshot.RoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A capture, a smoke's changes, then a restore: the current and confirmed resolution and window mode, vsync and the
// frame-rate limit are the player's again, exactly the changed elements are counted, and a second restore changes
// nothing
bool FSOLSettingsSnapshotRoundTripTest::RunTest(const FString& /*parameters*/)
{
    UGameUserSettings* settings = SettingsSnapshotTestPlayer();
    const FSOLEngineSettingsSnapshot snapshot = FSOLEngineSettingsSnapshot::Capture(*settings);
    TestTrue(TEXT("captured"), snapshot.IsCaptured());
    const FString* mode = snapshot.FindValue(TEXT("FullscreenMode"));
    const FString* confirmedMode = snapshot.FindValue(TEXT("LastConfirmedFullscreenMode"));
    TestEqual(TEXT("window mode captured"), mode != nullptr ? *mode : FString(),
        FString::FromInt(static_cast<int32>(EWindowMode::WindowedFullscreen)));
    TestEqual(TEXT("confirmed window mode captured"), confirmedMode != nullptr ? *confirmedMode : FString(),
        FString::FromInt(static_cast<int32>(EWindowMode::Fullscreen)));
    SettingsSnapshotTestSmoke(*settings);
    TestEqual(TEXT("the smoke confirmed its size"), settings->GetLastConfirmedScreenResolution(),
        SETTINGSSNAPSHOT_TEST_SMOKE);

    TestEqual(TEXT("changed elements restored"), snapshot.RestoreTo(*settings), SETTINGSSNAPSHOT_TEST_CHANGED);
    TestEqual(TEXT("resolution"), settings->GetScreenResolution(), SETTINGSSNAPSHOT_TEST_CURRENT);
    TestEqual(TEXT("confirmed resolution"), settings->GetLastConfirmedScreenResolution(),
        SETTINGSSNAPSHOT_TEST_CONFIRMED);
    TestEqual(TEXT("window mode"), static_cast<int32>(settings->GetFullscreenMode()),
        static_cast<int32>(EWindowMode::WindowedFullscreen));
    TestEqual(TEXT("confirmed window mode"), static_cast<int32>(settings->GetLastConfirmedFullscreenMode()),
        static_cast<int32>(EWindowMode::Fullscreen));
    TestTrue(TEXT("vsync"), settings->IsVSyncEnabled());
    TestEqual(TEXT("frame-rate limit"), settings->GetFrameRateLimit(), SETTINGSSNAPSHOT_TEST_FRAME_LIMIT);
    TestEqual(TEXT("a second restore changes nothing"), snapshot.RestoreTo(*settings), 0);

    // A capture after the restore reads exactly what the first one did (each differing element named)
    const FSOLEngineSettingsSnapshot again = FSOLEngineSettingsSnapshot::Capture(*settings);
    TestEqual(TEXT("as many elements"), again.Values.Num(), snapshot.Values.Num());
    for (int32 index = 0; index < again.Values.Num() && index < snapshot.Values.Num(); ++index)
    {
        const FSOLEngineSettingsSnapshot::FValue& value = again.Values[index];
        TestEqual(*FString::Printf(TEXT("%s[%d] restored"), *value.Property.ToString(), value.Index),
            value.Property.ToString() + TEXT("=") + value.Text, snapshot.Values[index].Property.ToString()
            + TEXT("=") + snapshot.Values[index].Text);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsSnapshotCoverageTest, "SOLTest.SettingsSnapshot.Coverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every config property element SaveSettings writes is copied (the confirmed and preferred window modes, the desired
// screen size and the window positions included), with its text as config writes it
bool FSOLSettingsSnapshotCoverageTest::RunTest(const FString& /*parameters*/)
{
    UGameUserSettings* settings = SettingsSnapshotTestPlayer();
    const FSOLEngineSettingsSnapshot snapshot = FSOLEngineSettingsSnapshot::Capture(*settings);
    int32 elements = 0;
    for (TFieldIterator<FProperty> it(UGameUserSettings::StaticClass()); it; ++it)
    {
        if (it->HasAnyPropertyFlags(CPF_Config))
        {
            elements += it->ArrayDim;
            TestNotNull(*FString::Printf(TEXT("%s copied"), *it->GetName()), snapshot.FindValue(it->GetFName()));
        }
    }
    TestTrue(TEXT("config properties exist"), elements > 0);
    TestEqual(TEXT("one value per config element"), snapshot.Values.Num(), elements);
    const TCHAR* named[] = { TEXT("ResolutionSizeX"), TEXT("LastUserConfirmedResolutionSizeX"), TEXT("FullscreenMode"),
        TEXT("LastConfirmedFullscreenMode"), TEXT("PreferredFullscreenMode"), TEXT("bUseVSync"),
        TEXT("FrameRateLimit"), TEXT("DesiredScreenWidth"), TEXT("WindowPositions") };
    for (const TCHAR* name : named)
    {
        TestNotNull(*FString::Printf(TEXT("%s in the snapshot"), name), snapshot.FindValue(FName(name)));
    }
    const FString* width = snapshot.FindValue(TEXT("ResolutionSizeX"));
    const FString* confirmed = snapshot.FindValue(TEXT("LastUserConfirmedResolutionSizeX"));
    TestTrue(TEXT("current width as text"), width != nullptr
        && *width == FString::FromInt(SETTINGSSNAPSHOT_TEST_CURRENT.X));
    TestTrue(TEXT("confirmed width as text"), confirmed != nullptr
        && *confirmed == FString::FromInt(SETTINGSSNAPSHOT_TEST_CONFIRMED.X));
    TestNull(TEXT("a property that is not there"), snapshot.FindValue(TEXT("NotASetting")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSettingsSnapshotEmptyTest, "SOLTest.SettingsSnapshot.Empty",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An empty snapshot (not a smoke run) restores nothing; a restore with nothing changed counts nothing
bool FSOLSettingsSnapshotEmptyTest::RunTest(const FString& /*parameters*/)
{
    UGameUserSettings* settings = SettingsSnapshotTestPlayer();
    const FSOLEngineSettingsSnapshot empty;
    TestFalse(TEXT("not captured"), empty.IsCaptured());
    TestNull(TEXT("no values"), empty.FindValue(TEXT("ResolutionSizeX")));
    SettingsSnapshotTestSmoke(*settings);
    TestEqual(TEXT("an empty snapshot restores nothing"), empty.RestoreTo(*settings), 0);
    TestEqual(TEXT("the smoke's size stays"), settings->GetScreenResolution(), SETTINGSSNAPSHOT_TEST_SMOKE);
    TestFalse(TEXT("an empty snapshot leaves the scalability levels"), empty.RestoreQualityLevels());

    const FSOLEngineSettingsSnapshot unchanged = FSOLEngineSettingsSnapshot::Capture(*settings);
    TestEqual(TEXT("nothing changed since the capture"), unchanged.RestoreTo(*settings), 0);
    TestFalse(TEXT("the engine's scalability levels unchanged since the capture"), unchanged.RestoreQualityLevels());
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
