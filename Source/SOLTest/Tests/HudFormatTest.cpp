/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHudFormat.h"

#include "Flight/SOLFlightMode.h"
#include "Menu/SOLBindingLabel.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuText.h"

#include "SOLConstants.h"
#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Light speed as the contract states it (independent of the code under test)
    constexpr double HUDFORMAT_C_MPS = 299792458.0;

    //////////////////////////////////////////////////////////////////////////
    // Returns a readable name for a speed unit, for test messages
    const TCHAR* HudFormatUnitName(const ESOLSpeedUnit unit)
    {
        switch (unit)
        {
        case ESOLSpeedUnit::MetersPerSecond:
            return TEXT("m/s");
        case ESOLSpeedUnit::KilometersPerSecond:
            return TEXT("km/s");
        case ESOLSpeedUnit::LightSpeed:
            return TEXT("c");
        }
        return TEXT("?");
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts PickSpeedUnit returns the expected unit for a speed
    void HudFormatExpectUnit(FAutomationTestBase& test, const double speedMps, const ESOLSpeedUnit expected)
    {
        const ESOLSpeedUnit actual = SOLHudFormat::PickSpeedUnit(speedMps);
        test.TestTrue(FString::Printf(TEXT("PickSpeedUnit(%.6f) is %s (got %s)"), speedMps, HudFormatUnitName(expected),
            HudFormatUnitName(actual)), actual == expected);
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts FormatDistanceM produces an exact string
    void HudFormatExpectDistance(FAutomationTestBase& test, const double distanceM, const TCHAR* expected)
    {
        test.TestEqual(FString::Printf(TEXT("FormatDistanceM(%.3f)"), distanceM), SOLHudFormat::FormatDistanceM(distanceM),
            FString(expected));
    }
}

// --- PickSpeedUnit ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatPickMetersKmTest, "SOLTest.HudFormat.PickUnitMetersToKmBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Zero and speeds under 1000 m/s pick m/s; 1000 m/s and above (below 0.01c) pick km/s
bool FSOLHudFormatPickMetersKmTest::RunTest(const FString& /*parameters*/)
{
    HudFormatExpectUnit(*this, 0.0, ESOLSpeedUnit::MetersPerSecond);
    HudFormatExpectUnit(*this, 1.0, ESOLSpeedUnit::MetersPerSecond);
    HudFormatExpectUnit(*this, 999.0, ESOLSpeedUnit::MetersPerSecond);
    HudFormatExpectUnit(*this, 999.9999, ESOLSpeedUnit::MetersPerSecond);
    HudFormatExpectUnit(*this, 1000.0, ESOLSpeedUnit::KilometersPerSecond);
    HudFormatExpectUnit(*this, 1000.0001, ESOLSpeedUnit::KilometersPerSecond);
    HudFormatExpectUnit(*this, 29780.0, ESOLSpeedUnit::KilometersPerSecond);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatPickLightTest, "SOLTest.HudFormat.PickUnitKmToLightBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Speeds just under 0.01c pick km/s; just over 0.01c and up to the max cap pick c
bool FSOLHudFormatPickLightTest::RunTest(const FString& /*parameters*/)
{
    const double onePercentC = 0.01 * HUDFORMAT_C_MPS;
    HudFormatExpectUnit(*this, 2997924.0, ESOLSpeedUnit::KilometersPerSecond);
    HudFormatExpectUnit(*this, onePercentC * (1.0 - 1e-9), ESOLSpeedUnit::KilometersPerSecond);
    HudFormatExpectUnit(*this, onePercentC * (1.0 + 1e-9), ESOLSpeedUnit::LightSpeed);
    HudFormatExpectUnit(*this, 3000000.0, ESOLSpeedUnit::LightSpeed);
    HudFormatExpectUnit(*this, 0.1 * HUDFORMAT_C_MPS, ESOLSpeedUnit::LightSpeed);
    HudFormatExpectUnit(*this, SOL::MAX_SPEED_CAP_MPS, ESOLSpeedUnit::LightSpeed);
    HudFormatExpectUnit(*this, HUDFORMAT_C_MPS, ESOLSpeedUnit::LightSpeed);
    return true;
}

// --- ToUnitValue / FromUnitValue ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatToUnitTest, "SOLTest.HudFormat.ToUnitValueFactors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// m/s is unchanged, km/s divides by 1000, c divides by exactly 299792458.0
bool FSOLHudFormatToUnitTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("m/s unchanged"), SOLHudFormat::ToUnitValue(123.45, ESOLSpeedUnit::MetersPerSecond), 123.45, 1e-12);
    TestEqual(TEXT("1500 m/s = 1.5 km/s"), SOLHudFormat::ToUnitValue(1500.0, ESOLSpeedUnit::KilometersPerSecond), 1.5,
        1e-15);
    TestEqual(TEXT("c in c units = 1"), SOLHudFormat::ToUnitValue(HUDFORMAT_C_MPS, ESOLSpeedUnit::LightSpeed), 1.0, 1e-15);
    TestEqual(TEXT("Max cap = 0.5 c"), SOLHudFormat::ToUnitValue(SOL::MAX_SPEED_CAP_MPS, ESOLSpeedUnit::LightSpeed), 0.5,
        1e-15);
    TestEqual(TEXT("1 m/s in c units"), SOLHudFormat::ToUnitValue(1.0, ESOLSpeedUnit::LightSpeed), 1.0 / HUDFORMAT_C_MPS,
        1e-24);
    TestEqual(TEXT("Zero km/s"), SOLHudFormat::ToUnitValue(0.0, ESOLSpeedUnit::KilometersPerSecond), 0.0, 0.0);
    TestEqual(TEXT("FromUnitValue km/s multiplies by 1000"),
        SOLHudFormat::FromUnitValue(2.5, ESOLSpeedUnit::KilometersPerSecond), 2500.0, 1e-12);
    TestEqual(TEXT("FromUnitValue c multiplies by light speed"),
        SOLHudFormat::FromUnitValue(0.25, ESOLSpeedUnit::LightSpeed), 0.25 * HUDFORMAT_C_MPS, 1e-6);
    TestEqual(TEXT("FromUnitValue m/s unchanged"), SOLHudFormat::FromUnitValue(42.0, ESOLSpeedUnit::MetersPerSecond), 42.0,
        1e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatRoundTripTest, "SOLTest.HudFormat.UnitRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// FromUnitValue(ToUnitValue(x)) returns x for every unit across magnitudes, including 0 and the max cap
bool FSOLHudFormatRoundTripTest::RunTest(const FString& /*parameters*/)
{
    const double speeds[] = { 0.0, 1.0, 0.37, 999.0, 1000.0, 29780.0, 2997924.58, 0.1 * HUDFORMAT_C_MPS,
        SOL::MAX_SPEED_CAP_MPS * (1.0 - 1e-12), SOL::MAX_SPEED_CAP_MPS };
    const ESOLSpeedUnit units[] = { ESOLSpeedUnit::MetersPerSecond, ESOLSpeedUnit::KilometersPerSecond,
        ESOLSpeedUnit::LightSpeed };

    // Every speed through every unit and back
    for (const ESOLSpeedUnit unit : units)
    {
        for (const double speed : speeds)
        {
            const double back = SOLHudFormat::FromUnitValue(SOLHudFormat::ToUnitValue(speed, unit), unit);
            const bool bNear = speed == 0.0 ? back == 0.0 : SOLTestHelpers::RelativeError(back, speed) < 1e-14;
            TestTrue(FString::Printf(TEXT("Round trip %.6f m/s via %s (got %.6f)"), speed, HudFormatUnitName(unit), back),
                bNear);
        }
    }
    return true;
}

// --- FormatDistanceM ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatDistanceMetersTest, "SOLTest.HudFormat.DistanceMetersBand",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Under 1000 m prints meters with one decimal
bool FSOLHudFormatDistanceMetersTest::RunTest(const FString& /*parameters*/)
{
    HudFormatExpectDistance(*this, 0.0, TEXT("0.0 m"));
    HudFormatExpectDistance(*this, 7.0, TEXT("7.0 m"));
    HudFormatExpectDistance(*this, 512.3, TEXT("512.3 m"));
    HudFormatExpectDistance(*this, 999.9, TEXT("999.9 m"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatDistanceKmTest, "SOLTest.HudFormat.DistanceKilometersBand",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// From 1000 m up to just under 0.01 AU prints kilometers with one decimal
bool FSOLHudFormatDistanceKmTest::RunTest(const FString& /*parameters*/)
{
    HudFormatExpectDistance(*this, 1000.0, TEXT("1.0 km"));
    HudFormatExpectDistance(*this, 1500.0, TEXT("1.5 km"));
    HudFormatExpectDistance(*this, 23402900.0, TEXT("23402.9 km"));
    HudFormatExpectDistance(*this, 384400000.0, TEXT("384400.0 km"));

    // 0.01 AU = 1495978707 m; one meter below stays in km
    HudFormatExpectDistance(*this, 1495978706.0, TEXT("1495978.7 km"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatDistanceAuTest, "SOLTest.HudFormat.DistanceAstronomicalUnitBand",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At and above 0.01 AU prints AU with four decimals, including very large distances
bool FSOLHudFormatDistanceAuTest::RunTest(const FString& /*parameters*/)
{
    // One meter above 0.01 AU (1495978707 m); the exact boundary is left to floating-point comparison order
    HudFormatExpectDistance(*this, 1495978708.0, TEXT("0.0100 AU"));
    HudFormatExpectDistance(*this, SOLTestHelpers::AU_M, TEXT("1.0000 AU"));
    HudFormatExpectDistance(*this, 1.5 * SOLTestHelpers::AU_M, TEXT("1.5000 AU"));
    HudFormatExpectDistance(*this, 30.0 * SOLTestHelpers::AU_M, TEXT("30.0000 AU"));
    HudFormatExpectDistance(*this, 50.0 * SOLTestHelpers::AU_M, TEXT("50.0000 AU"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatAppendDistanceTest, "SOLTest.HudFormat.AppendDistanceMatchesFormat",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AppendDistanceM appends exactly FormatDistanceM's text after any existing buffer content, in every band
bool FSOLHudFormatAppendDistanceTest::RunTest(const FString& /*parameters*/)
{
    const double distancesM[] = { 0.0, 7.0, 999.9, 1000.0, 23402900.0, 1495978706.0, 1495978708.0,
        SOLTestHelpers::AU_M, 50.0 * SOLTestHelpers::AU_M, 1.0e7 };

    // Into an empty buffer and after a prefix, the appended text equals the formatted string
    for (const double distanceM : distancesM)
    {
        const FString expected = SOLHudFormat::FormatDistanceM(distanceM);
        FString empty;
        SOLHudFormat::AppendDistanceM(empty, distanceM);
        TestEqual(FString::Printf(TEXT("AppendDistanceM(%.3f) into empty"), distanceM), empty, expected);
        FString prefixed(TEXT("RADAR "));
        SOLHudFormat::AppendDistanceM(prefixed, distanceM);
        TestEqual(FString::Printf(TEXT("AppendDistanceM(%.3f) after prefix"), distanceM), prefixed,
            FString(TEXT("RADAR ")) + expected);
    }

    // The radar's default floor reads as the in-game label shows it
    FString label;
    SOLHudFormat::AppendDistanceM(label, 1.0e7);
    TestEqual(TEXT("10,000 km floor label"), label, FString(TEXT("10000.0 km")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatKeyHintsTest, "SOLTest.HudFormat.KeyHints",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The key hint lines read the player's bindings: the defaults give the original text, a rebind shows its new key
bool FSOLHudFormatKeyHintsTest::RunTest(const FString& /*parameters*/)
{
    TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    TestEqual(TEXT("default flight hints, orbits off"), SOLHudFormat::FormatFlightKeyHints(keys, false),
        FString(TEXT("ORBITS OFF (O)   F3 speed panel   Shift+M map   T/R/F/X target   M match frame   L surface-lock"
        "   [ ] warp   - = radar zoom   Home radar auto   LMB fire   G drop target   P routes   V resume")));
    TestTrue(TEXT("orbits on"), SOLHudFormat::FormatFlightKeyHints(keys, true).StartsWith(TEXT("ORBITS ON (O)   ")));
    TestEqual(TEXT("default map hints"), SOLHudFormat::FormatMapKeyHints(keys), FString(TEXT("MAP   J jump   M move   "
        "P patrol   right drag orbit   middle or Shift+right drag pan   wheel zoom   Shift+M / Esc close")));

    keys[static_cast<int32>(SOLKeyBindings::EBinding::Fire)] = TEXT("K");
    keys[static_cast<int32>(SOLKeyBindings::EBinding::OpenMap)] = TEXT("N");
    keys[static_cast<int32>(SOLKeyBindings::EBinding::ToggleOrbitLines)] = TEXT("SpaceBar");
    const FString rebound = SOLHudFormat::FormatFlightKeyHints(keys, false);
    TestTrue(TEXT("rebound fire shown"), rebound.Contains(TEXT("   K fire   ")));
    TestFalse(TEXT("old fire key gone"), rebound.Contains(TEXT("LMB")));
    TestTrue(TEXT("rebound map key shown"), rebound.Contains(TEXT("   Shift+N map   ")));
    TestTrue(TEXT("rebound orbit key shown"), rebound.StartsWith(TEXT("ORBITS OFF (Space)")));
    TestTrue(TEXT("rebound map close key"), SOLHudFormat::FormatMapKeyHints(keys).EndsWith(
        TEXT("Shift+N / Esc close")));
    TestTrue(TEXT("short key array reads unbound"), SOLHudFormat::FormatMapKeyHints(TArray<FString>())
        .EndsWith(TEXT("Unbound / Esc close")));
    return true;
}

// --- AppendDurationS ---

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Asserts AppendDurationS writes an exact string into an empty buffer
    void HudFormatExpectDuration(FAutomationTestBase& test, const double seconds, const TCHAR* expected)
    {
        FString text;
        SOLHudFormat::AppendDurationS(text, seconds);
        test.TestEqual(FString::Printf(TEXT("AppendDurationS(%.3f)"), seconds), text, FString(expected));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatAppendDurationTest, "SOLTest.HudFormat.AppendDurationS",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Rounds to whole seconds (half up), then "<s> s", "M:SS", "H:MM:SS" or "<d> d <hh> h"; "--" when invalid; appends
bool FSOLHudFormatAppendDurationTest::RunTest(const FString& /*parameters*/)
{
    // Under a minute (after rounding)
    HudFormatExpectDuration(*this, 0.0, TEXT("0 s"));
    HudFormatExpectDuration(*this, 0.4, TEXT("0 s"));
    HudFormatExpectDuration(*this, 0.5, TEXT("1 s"));
    HudFormatExpectDuration(*this, 45.4, TEXT("45 s"));
    HudFormatExpectDuration(*this, 59.0, TEXT("59 s"));
    HudFormatExpectDuration(*this, 59.5, TEXT("1:00"));
    HudFormatExpectDuration(*this, 59.6, TEXT("1:00"));

    // Minutes: M:SS
    HudFormatExpectDuration(*this, 60.0, TEXT("1:00"));
    HudFormatExpectDuration(*this, 725.0, TEXT("12:05"));
    HudFormatExpectDuration(*this, 3599.0, TEXT("59:59"));
    HudFormatExpectDuration(*this, 3599.5, TEXT("1:00:00"));

    // Hours: H:MM:SS
    HudFormatExpectDuration(*this, 3600.0, TEXT("1:00:00"));
    HudFormatExpectDuration(*this, 10927.0, TEXT("3:02:07"));
    HudFormatExpectDuration(*this, 86399.0, TEXT("23:59:59"));

    // Days: "<d> d <hh> h", minutes and seconds dropped
    HudFormatExpectDuration(*this, 86399.5, TEXT("1 d 00 h"));
    HudFormatExpectDuration(*this, 86400.0, TEXT("1 d 00 h"));
    HudFormatExpectDuration(*this, 90061.0, TEXT("1 d 01 h"));
    HudFormatExpectDuration(*this, 1166400.0, TEXT("13 d 12 h"));
    HudFormatExpectDuration(*this, 1166400.0 + 3599.0, TEXT("13 d 12 h"));
    HudFormatExpectDuration(*this, 1000.0 * 86400.0, TEXT("1000 d 00 h"));

    // Invalid: negative or non-finite
    HudFormatExpectDuration(*this, -1.0, TEXT("--"));
    HudFormatExpectDuration(*this, -1000.0, TEXT("--"));
    HudFormatExpectDuration(*this, std::numeric_limits<double>::quiet_NaN(), TEXT("--"));
    HudFormatExpectDuration(*this, std::numeric_limits<double>::infinity(), TEXT("--"));
    HudFormatExpectDuration(*this, -std::numeric_limits<double>::infinity(), TEXT("--"));

    // Appends after existing text, never resets it
    FString text(TEXT("ETA "));
    SOLHudFormat::AppendDurationS(text, 725.0);
    TestEqual(TEXT("Appends after a prefix"), text, FString(TEXT("ETA 12:05")));
    SOLHudFormat::AppendDurationS(text, -1.0);
    TestEqual(TEXT("Appends again, including the invalid marker"), text, FString(TEXT("ETA 12:05--")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatKeyTextsTest, "SOLTest.HudFormat.KeyTexts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The surface-lock hint, frame-lock tag, no-target text and speed panel title name the player's keys
bool FSOLHudFormatKeyTextsTest::RunTest(const FString& /*parameters*/)
{
    using SOLKeyBindings::EBinding;
    TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    SOLHudFormat::FHudKeyTexts texts = SOLHudFormat::FormatHudKeyTexts(keys);
    TestEqual(TEXT("default surface-lock hint"), texts.SurfaceLockHint, FString(TEXT("L to surface-lock  ")));
    TestEqual(TEXT("default frame-lock tag"), texts.FrameLockTag, FString(TEXT(" [M LOCK]")));
    TestEqual(TEXT("default no-target text"), texts.NoTarget, FString(TEXT("none (T)")));
    TestEqual(TEXT("default speed panel title"), texts.SpeedPanelTitle,
        FString(TEXT("SPEED CAP  (F3 / Enter close, Esc close)")));
    TestEqual(TEXT("default route resume hint"), texts.RouteResumeHint, FString(TEXT("   V resume")));

    keys[static_cast<int32>(EBinding::SurfaceLock)] = TEXT("K");
    keys[static_cast<int32>(EBinding::MatchLock)] = TEXT("N");
    keys[static_cast<int32>(EBinding::SelectTarget)] = TEXT("MiddleMouseButton");
    keys[static_cast<int32>(EBinding::SpeedPanel)] = TEXT("F4");
    keys[static_cast<int32>(EBinding::Resume)] = TEXT("B");
    texts = SOLHudFormat::FormatHudKeyTexts(keys);
    TestEqual(TEXT("rebound route resume hint"), texts.RouteResumeHint, FString(TEXT("   B resume")));
    TestEqual(TEXT("rebound surface-lock hint"), texts.SurfaceLockHint, FString(TEXT("K to surface-lock  ")));
    TestEqual(TEXT("rebound frame-lock tag"), texts.FrameLockTag, FString(TEXT(" [N LOCK]")));
    TestEqual(TEXT("rebound no-target text"), texts.NoTarget, FString(TEXT("none (MMB)")));
    TestEqual(TEXT("rebound speed panel title"), texts.SpeedPanelTitle,
        FString(TEXT("SPEED CAP  (F4 / Enter close, Esc close)")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatSteerHintTest, "SOLTest.HudFormat.SteerHint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The first-flight steer hint names the live steer-hold key and matches HUD_STEER_HINT_FORMAT filled with the keys
bool FSOLHudFormatSteerHintTest::RunTest(const FString& /*parameters*/)
{
    using SOLKeyBindings::EBinding;
    TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    SOLHudFormat::FHudKeyTexts texts = SOLHudFormat::FormatHudKeyTexts(keys);
    TestEqual(TEXT("default steer hint"), texts.SteerHint, FString(TEXT("Hold Space to steer")));
    TestEqual(TEXT("default steer hint follows the format"), texts.SteerHint,
        SOLKeyBindings::FormatKeyText(SOLMenuText::HUD_STEER_HINT_FORMAT, keys));

    // Rebinding the steer-hold key changes only the hint's key; the other texts stay as they were
    const SOLHudFormat::FHudKeyTexts defaults = texts;
    keys[static_cast<int32>(EBinding::SteerHold)] = TEXT("F");
    texts = SOLHudFormat::FormatHudKeyTexts(keys);
    TestEqual(TEXT("rebound steer hint"), texts.SteerHint, FString(TEXT("Hold F to steer")));
    TestEqual(TEXT("rebound steer hint follows the format"), texts.SteerHint,
        SOLKeyBindings::FormatKeyText(SOLMenuText::HUD_STEER_HINT_FORMAT, keys));
    TestFalse(TEXT("rebound steer hint has no placeholder"), texts.SteerHint.Contains(TEXT("{")));
    TestEqual(TEXT("surface-lock hint unchanged"), texts.SurfaceLockHint, defaults.SurfaceLockHint);
    TestEqual(TEXT("frame-lock tag unchanged"), texts.FrameLockTag, defaults.FrameLockTag);
    TestEqual(TEXT("no-target text unchanged"), texts.NoTarget, defaults.NoTarget);
    TestEqual(TEXT("speed panel title unchanged"), texts.SpeedPanelTitle, defaults.SpeedPanelTitle);
    TestEqual(TEXT("route resume hint unchanged"), texts.RouteResumeHint, defaults.RouteResumeHint);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatPilotCircleColorTest, "SOLTest.HudFormat.PilotCircleColor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The joystick circles are yellow (FREE_LOOK) while free-looking and the primary blue-cyan otherwise
bool FSOLHudFormatPilotCircleColorTest::RunTest(const FString& /*parameters*/)
{
    const FLinearColor freeLook = SOLHudFormat::PilotCircleColor(true);
    const FLinearColor normal = SOLHudFormat::PilotCircleColor(false);
    TestTrue(TEXT("free-looking uses FREE_LOOK"), freeLook.Equals(SOL::HudPalette::FREE_LOOK, 1e-6f));
    TestTrue(TEXT("not free-looking uses PRIMARY"), normal.Equals(SOL::HudPalette::PRIMARY, 1e-6f));

    // FREE_LOOK is an opaque yellow, distinct from PRIMARY
    const FLinearColor yellow = SOL::HudPalette::FREE_LOOK;
    TestTrue(TEXT("FREE_LOOK red is high"), yellow.R >= 0.8f);
    TestTrue(TEXT("FREE_LOOK green is high"), yellow.G >= 0.6f);
    TestTrue(TEXT("FREE_LOOK blue is low"), yellow.B <= 0.4f);
    TestEqual(TEXT("FREE_LOOK is opaque"), yellow.A, 1.0f);
    TestFalse(TEXT("FREE_LOOK differs from PRIMARY"), yellow.Equals(SOL::HudPalette::PRIMARY, 0.01f));
    TestFalse(TEXT("the two states differ"), freeLook.Equals(normal, 0.01f));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatControlsChangedNoticeTest, "SOLTest.HudFormat.ControlsChangedNotice",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The one-time "default controls changed" notice names the live keys of its five bindings
bool FSOLHudFormatControlsChangedNoticeTest::RunTest(const FString& /*parameters*/)
{
    // Builds the expected notice text from five key names
    const auto expectedNotice = [](const TCHAR* hold, const TCHAR* toggle, const TCHAR* up, const TCHAR* boost,
                                    const TCHAR* assist)
    {
        return FString::Printf(
            TEXT("Default controls changed: %s or %s steers, %s thrust up, %s boost, %s assist. See Controls (Esc)"),
            *SOLBindingLabel::KeyDisplayName(hold), *SOLBindingLabel::KeyDisplayName(toggle),
            *SOLBindingLabel::KeyDisplayName(up), *SOLBindingLabel::KeyDisplayName(boost),
            *SOLBindingLabel::KeyDisplayName(assist));
    };

    TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    SOLHudFormat::FHudKeyTexts texts = SOLHudFormat::FormatHudKeyTexts(keys);
    TestEqual(TEXT("default notice"), texts.ControlsChangedNotice,
        expectedNotice(TEXT("SpaceBar"), TEXT("MiddleMouseButton"), TEXT("LeftShift"), TEXT("Tab"), TEXT("C")));

    keys[static_cast<int32>(SOLKeyBindings::FindById(TEXT("SteerHold")))] = TEXT("K");
    keys[static_cast<int32>(SOLKeyBindings::FindById(TEXT("SteerToggle")))] = TEXT("RightMouseButton");
    keys[static_cast<int32>(SOLKeyBindings::FindById(TEXT("ThrustUp")))] = TEXT("Home");
    keys[static_cast<int32>(SOLKeyBindings::FindById(TEXT("Boost")))] = TEXT("RightShift");
    keys[static_cast<int32>(SOLKeyBindings::FindById(TEXT("ToggleAssist")))] = TEXT("V");
    texts = SOLHudFormat::FormatHudKeyTexts(keys);
    TestEqual(TEXT("rebound notice"), texts.ControlsChangedNotice,
        expectedNotice(TEXT("K"), TEXT("RightMouseButton"), TEXT("Home"), TEXT("RightShift"), TEXT("V")));
    TestFalse(TEXT("rebound notice has no unresolved placeholder"), texts.ControlsChangedNotice.Contains(TEXT("{")));
    TestFalse(TEXT("rebound notice has no stale Boost key"), texts.ControlsChangedNotice.Contains(TEXT(" Tab boost")));
    TestFalse(TEXT("rebound notice has no stale assist key"), texts.ControlsChangedNotice.Contains(TEXT(" C assist")));
    return true;
}

// --- Flight mode label and color ---

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Asserts FlightModeLabel returns a non-null, exact string for a mode
    void HudFormatExpectModeLabel(FAutomationTestBase& test, const ESOLFlightMode mode, const TCHAR* expected)
    {
        const TCHAR* label = SOLHudFormat::FlightModeLabel(mode);
        test.TestNotNull(FString::Printf(TEXT("FlightModeLabel(%s) is not null"), expected), label);
        if (label != nullptr)
        {
            test.TestEqual(FString::Printf(TEXT("FlightModeLabel(%s)"), expected), FString(label), FString(expected));
        }
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatFlightModeLabelsTest, "SOLTest.HudFormat.FlightModeLabels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Each flight mode has its upper-case HUD label: MANUAL, ASSISTED, AUTO
bool FSOLHudFormatFlightModeLabelsTest::RunTest(const FString& /*parameters*/)
{
    HudFormatExpectModeLabel(*this, ESOLFlightMode::Manual, TEXT("MANUAL"));
    HudFormatExpectModeLabel(*this, ESOLFlightMode::Assisted, TEXT("ASSISTED"));
    HudFormatExpectModeLabel(*this, ESOLFlightMode::Auto, TEXT("AUTO"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudFormatFlightModeColorsTest, "SOLTest.HudFormat.FlightModeColors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Mode colors are the Limit Theory palette: Manual orange, Assisted blue-cyan, Auto green; opaque and pairwise distinct
bool FSOLHudFormatFlightModeColorsTest::RunTest(const FString& /*parameters*/)
{
    const FLinearColor manual = SOLHudFormat::FlightModeColor(ESOLFlightMode::Manual);
    const FLinearColor assisted = SOLHudFormat::FlightModeColor(ESOLFlightMode::Assisted);
    const FLinearColor autoColor = SOLHudFormat::FlightModeColor(ESOLFlightMode::Auto);

    // Expected values typed independently of the palette constants
    const FLinearColor expectedManual(1.0f, 0.3f, 0.1f, 1.0f);
    const FLinearColor expectedAssisted(0.2f, 0.7f, 1.0f, 1.0f);
    const FLinearColor expectedAuto(0.25f, 1.0f, 0.25f, 1.0f);

    TestTrue(FString::Printf(TEXT("Manual is LT orange: expected %s, actual %s"),
                 *expectedManual.ToString(), *manual.ToString()),
        manual.Equals(expectedManual, 1e-4f));
    TestTrue(FString::Printf(TEXT("Assisted is LT blue-cyan: expected %s, actual %s"),
                 *expectedAssisted.ToString(), *assisted.ToString()),
        assisted.Equals(expectedAssisted, 1e-4f));
    TestTrue(FString::Printf(TEXT("Auto is LT green: expected %s, actual %s"),
                 *expectedAuto.ToString(), *autoColor.ToString()),
        autoColor.Equals(expectedAuto, 1e-4f));

    // Opaque
    TestEqual(TEXT("Manual alpha is 1"), manual.A, 1.0f);
    TestEqual(TEXT("Assisted alpha is 1"), assisted.A, 1.0f);
    TestEqual(TEXT("Auto alpha is 1"), autoColor.A, 1.0f);

    // Pairwise distinct
    TestTrue(TEXT("Manual differs from Assisted"), !manual.Equals(assisted, 0.01f));
    TestTrue(TEXT("Manual differs from Auto"), !manual.Equals(autoColor, 0.01f));
    TestTrue(TEXT("Assisted differs from Auto"), !assisted.Equals(autoColor, 0.01f));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
