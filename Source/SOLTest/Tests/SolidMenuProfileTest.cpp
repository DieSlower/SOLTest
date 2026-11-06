/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSolidMenuProfileSmoke.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the -SOLSmokeSolidMenuProfile verdict maths (issue #46 Part 46h, Docs/SDDs/46-solid-menus.md section 13,
// Q17 as moved by the user on 2026-11-06: under 0.2 ms extra CPU and 0.3 ms extra GPU per screen; the VR floor of
// Docs/Research/VR_PERFORMANCE_TARGETS.md section 5a). The expected numbers are written out here, not read from the
// production constants, so a silent change of a threshold fails a test
namespace
{
    constexpr double SOLIDPROFILETEST_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double SOLIDPROFILETEST_INF = std::numeric_limits<double>::infinity();
    constexpr double SOLIDPROFILETEST_TOLERANCE = 1.0e-9;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuProfileBudgetTest, "SOLTest.SolidMenuProfile.Budget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The budget is under 0.2 ms CPU and 0.3 ms GPU extra, both strict; a non-finite delta fails
bool FSOLSolidMenuProfileBudgetTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("BUDGET_CPU_MS"), SOLSolidMenuProfile::BUDGET_CPU_MS, 0.2, SOLIDPROFILETEST_TOLERANCE);
    TestEqual(TEXT("BUDGET_GPU_MS"), SOLSolidMenuProfile::BUDGET_GPU_MS, 0.3, SOLIDPROFILETEST_TOLERANCE);
    TestTrue(TEXT("no extra cost"), SOLSolidMenuProfile::IsWithinBudget(0.0, 0.0));
    TestTrue(TEXT("cheaper than the baseline"), SOLSolidMenuProfile::IsWithinBudget(-0.05, -0.01));
    TestTrue(TEXT("0.15 ms CPU (over the old 0.1 ms budget)"), SOLSolidMenuProfile::IsWithinBudget(0.15, 0.1));
    TestTrue(TEXT("just under both"), SOLSolidMenuProfile::IsWithinBudget(0.199, 0.299));
    TestFalse(TEXT("CPU at the line"), SOLSolidMenuProfile::IsWithinBudget(0.2, 0.0));
    TestFalse(TEXT("GPU at the line"), SOLSolidMenuProfile::IsWithinBudget(0.0, 0.3));
    TestFalse(TEXT("CPU over"), SOLSolidMenuProfile::IsWithinBudget(0.25, 0.0));
    TestFalse(TEXT("GPU over"), SOLSolidMenuProfile::IsWithinBudget(0.0, 0.4));
    TestFalse(TEXT("CPU NaN"), SOLSolidMenuProfile::IsWithinBudget(SOLIDPROFILETEST_NAN, 0.0));
    TestFalse(TEXT("GPU NaN"), SOLSolidMenuProfile::IsWithinBudget(0.0, SOLIDPROFILETEST_NAN));
    TestFalse(TEXT("CPU -inf"), SOLSolidMenuProfile::IsWithinBudget(-SOLIDPROFILETEST_INF, 0.0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuProfileVrFloorTest, "SOLTest.SolidMenuProfile.VrFloor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// One Varjo XR-4 eye (3840x3744) and anything smaller is judged at 10.0 ms with a 1.2 ms menu GPU ceiling; more pixels
// than one eye (two eyes, 7680x3744) at 9.5 ms and 2.4 ms; a view without pixels has no floor
bool FSOLSolidMenuProfileVrFloorTest::RunTest(const FString& /*parameters*/)
{
    const SOLSolidMenuProfile::FVrFloor oneEye = SOLSolidMenuProfile::VrFloorForView(3840, 3744);
    TestTrue(TEXT("one eye applies"), oneEye.bApplies);
    TestFalse(TEXT("one eye is not two"), oneEye.bTwoEyes);
    TestEqual(TEXT("one eye line"), oneEye.LineMs, 10.0, SOLIDPROFILETEST_TOLERANCE);
    TestEqual(TEXT("one eye menu GPU ceiling"), oneEye.UiGpuMs, 1.2, SOLIDPROFILETEST_TOLERANCE);

    const SOLSolidMenuProfile::FVrFloor twoEyes = SOLSolidMenuProfile::VrFloorForView(7680, 3744);
    TestTrue(TEXT("two eyes apply"), twoEyes.bApplies);
    TestTrue(TEXT("two eyes"), twoEyes.bTwoEyes);
    TestEqual(TEXT("two eyes line"), twoEyes.LineMs, 9.5, SOLIDPROFILETEST_TOLERANCE);
    TestEqual(TEXT("two eyes menu GPU ceiling"), twoEyes.UiGpuMs, 2.4, SOLIDPROFILETEST_TOLERANCE);

    TestFalse(TEXT("2560x1440 is one eye"), SOLSolidMenuProfile::VrFloorForView(2560, 1440).bTwoEyes);
    TestFalse(TEXT("one pixel short of one eye"), SOLSolidMenuProfile::VrFloorForView(3840, 3743).bTwoEyes);
    TestTrue(TEXT("one row over one eye"), SOLSolidMenuProfile::VrFloorForView(3840, 3745).bTwoEyes);
    TestTrue(TEXT("a large view does not overflow"), SOLSolidMenuProfile::VrFloorForView(16384, 16384).bTwoEyes);
    TestFalse(TEXT("zero width"), SOLSolidMenuProfile::VrFloorForView(0, 3744).bApplies);
    TestFalse(TEXT("negative height"), SOLSolidMenuProfile::VrFloorForView(3840, -1).bApplies);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuProfileMenuShareTest, "SOLTest.SolidMenuProfile.MenuShare",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A view run judges only the menu's share (user, 2026-11-06): CPU delta under 0.2 ms and the menu's GPU under the
// floor's ceiling (1.2 ms one eye, 2.4 ms two eyes), both strict; non-finite numbers and a floor without pixels fail
bool FSOLSolidMenuProfileMenuShareTest::RunTest(const FString& /*parameters*/)
{
    const SOLSolidMenuProfile::FVrFloor oneEye = SOLSolidMenuProfile::VrFloorForView(3840, 3744);
    const SOLSolidMenuProfile::FVrFloor twoEyes = SOLSolidMenuProfile::VrFloorForView(7680, 3744);
    const SOLSolidMenuProfile::FVrFloor none = SOLSolidMenuProfile::VrFloorForView(0, 0);
    TestTrue(TEXT("cheap menu, one eye"), SOLSolidMenuProfile::IsMenuShareWithinVrFloor(0.1, 0.7, oneEye));
    TestTrue(TEXT("cheaper than the baseline"), SOLSolidMenuProfile::IsMenuShareWithinVrFloor(-0.1, 0.2, oneEye));
    TestFalse(TEXT("CPU at the line"), SOLSolidMenuProfile::IsMenuShareWithinVrFloor(0.2, 0.7, oneEye));
    TestFalse(TEXT("one eye GPU at the ceiling"), SOLSolidMenuProfile::IsMenuShareWithinVrFloor(0.0, 1.2, oneEye));
    TestTrue(TEXT("1.5 ms GPU fits two eyes"), SOLSolidMenuProfile::IsMenuShareWithinVrFloor(0.0, 1.5, twoEyes));
    TestFalse(TEXT("1.5 ms GPU misses one eye"), SOLSolidMenuProfile::IsMenuShareWithinVrFloor(0.0, 1.5, oneEye));
    TestFalse(TEXT("two eyes GPU at the ceiling"), SOLSolidMenuProfile::IsMenuShareWithinVrFloor(0.0, 2.4, twoEyes));
    TestFalse(TEXT("no floor"), SOLSolidMenuProfile::IsMenuShareWithinVrFloor(0.0, 0.1, none));
    TestFalse(TEXT("CPU NaN"), SOLSolidMenuProfile::IsMenuShareWithinVrFloor(SOLIDPROFILETEST_NAN, 0.1, oneEye));
    TestFalse(TEXT("GPU NaN"), SOLSolidMenuProfile::IsMenuShareWithinVrFloor(0.0, SOLIDPROFILETEST_NAN, oneEye));
    TestFalse(TEXT("CPU -inf"), SOLSolidMenuProfile::IsMenuShareWithinVrFloor(-SOLIDPROFILETEST_INF, 0.1, oneEye));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuProfileProxyTest, "SOLTest.SolidMenuProfile.Proxy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The whole-frame stand-in (VR doc "Proxy on this machine"): 3840x2160 only, the p99 frame at or under 10.0 ms
bool FSOLSolidMenuProfileProxyTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("4K is the proxy view"), SOLSolidMenuProfile::IsVrProxyView(3840, 2160));
    TestFalse(TEXT("one XR-4 eye is not"), SOLSolidMenuProfile::IsVrProxyView(3840, 3744));
    TestFalse(TEXT("1920x1080 is not"), SOLSolidMenuProfile::IsVrProxyView(1920, 1080));
    TestFalse(TEXT("transposed is not"), SOLSolidMenuProfile::IsVrProxyView(2160, 3840));
    TestTrue(TEXT("100 Hz exactly"), SOLSolidMenuProfile::MeetsVrProxy(10.0));
    TestTrue(TEXT("120 Hz"), SOLSolidMenuProfile::MeetsVrProxy(8.33));
    TestFalse(TEXT("just over"), SOLSolidMenuProfile::MeetsVrProxy(10.01));
    TestFalse(TEXT("not read"), SOLSolidMenuProfile::MeetsVrProxy(-1.0));
    TestFalse(TEXT("zero"), SOLSolidMenuProfile::MeetsVrProxy(0.0));
    TestFalse(TEXT("NaN"), SOLSolidMenuProfile::MeetsVrProxy(SOLIDPROFILETEST_NAN));
    TestFalse(TEXT("infinity"), SOLSolidMenuProfile::MeetsVrProxy(SOLIDPROFILETEST_INF));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuProfilePercentileTest, "SOLTest.SolidMenuProfile.Percentile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Nearest rank: the ceil(fraction x N)-th smallest value; the fraction clamped to [0, 1] and NaN read as 1; the values
// sorted in place; -1 for no values
bool FSOLSolidMenuProfilePercentileTest::RunTest(const FString& /*parameters*/)
{
    TArray<double> empty;
    TestEqual(TEXT("no values"), SOLSolidMenuProfile::Percentile(empty, 0.99), -1.0, SOLIDPROFILETEST_TOLERANCE);

    // 1..100 shuffled: p99 is the 99th smallest, p50 the 50th, p100 the largest, p0 the smallest
    TArray<double> hundred;
    for (int32 index = 0; index < 100; ++index)
    {
        hundred.Add(static_cast<double>((index * 37) % 100 + 1));
    }
    TestEqual(TEXT("p99 of 1..100"), SOLSolidMenuProfile::Percentile(hundred, 0.99), 99.0,
        SOLIDPROFILETEST_TOLERANCE);
    TestEqual(TEXT("sorted in place"), hundred[0], 1.0, SOLIDPROFILETEST_TOLERANCE);
    TestEqual(TEXT("p50 of 1..100"), SOLSolidMenuProfile::Percentile(hundred, 0.5), 50.0, SOLIDPROFILETEST_TOLERANCE);
    TestEqual(TEXT("p100"), SOLSolidMenuProfile::Percentile(hundred, 1.0), 100.0, SOLIDPROFILETEST_TOLERANCE);
    TestEqual(TEXT("p0"), SOLSolidMenuProfile::Percentile(hundred, 0.0), 1.0, SOLIDPROFILETEST_TOLERANCE);
    TestEqual(TEXT("fraction over 1"), SOLSolidMenuProfile::Percentile(hundred, 2.0), 100.0,
        SOLIDPROFILETEST_TOLERANCE);
    TestEqual(TEXT("negative fraction"), SOLSolidMenuProfile::Percentile(hundred, -1.0), 1.0,
        SOLIDPROFILETEST_TOLERANCE);
    TestEqual(TEXT("NaN fraction"), SOLSolidMenuProfile::Percentile(hundred, SOLIDPROFILETEST_NAN), 100.0,
        SOLIDPROFILETEST_TOLERANCE);

    // 240 frames (the smoke's sample count) with three spikes: p99 is the 238th smallest, one of the spikes
    TArray<double> frames;
    frames.Init(6.0, 240);
    frames[10] = 20.0;
    frames[100] = 30.0;
    frames[200] = 40.0;
    TestEqual(TEXT("p99 of 240 with 3 spikes"), SOLSolidMenuProfile::Percentile(frames, 0.99), 20.0,
        SOLIDPROFILETEST_TOLERANCE);
    TArray<double> single = { 7.5 };
    TestEqual(TEXT("one value"), SOLSolidMenuProfile::Percentile(single, 0.99), 7.5, SOLIDPROFILETEST_TOLERANCE);

    // (46i) Fractions whose product with 100 rounds up in double (0.07 x 100 = 7.000000000000001, likewise 0.14 and
    // 0.28) still give their exact rank
    TestEqual(TEXT("p7 of 1..100"), SOLSolidMenuProfile::Percentile(hundred, 0.07), 7.0, SOLIDPROFILETEST_TOLERANCE);
    TestEqual(TEXT("p14 of 1..100"), SOLSolidMenuProfile::Percentile(hundred, 0.14), 14.0, SOLIDPROFILETEST_TOLERANCE);
    TestEqual(TEXT("p28 of 1..100"), SOLSolidMenuProfile::Percentile(hundred, 0.28), 28.0, SOLIDPROFILETEST_TOLERANCE);
    TestEqual(TEXT("p7.5 of 1..100 rounds up to the 8th"), SOLSolidMenuProfile::Percentile(hundred, 0.075), 8.0,
        SOLIDPROFILETEST_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidMenuProfileFrameLineTest, "SOLTest.SolidMenuProfile.VrFrameLine",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// (46i) The p99 frame, game thread and GPU times meet a VR floor's line (10.0 ms one eye, 9.5 ms two eyes) only when
// each is positive, finite and at or under it; a floor that does not apply has no line
bool FSOLSolidMenuProfileFrameLineTest::RunTest(const FString& /*parameters*/)
{
    using SOLSolidMenuProfile::MeetsVrFrameLine;
    const SOLSolidMenuProfile::FVrFloor oneEye = SOLSolidMenuProfile::VrFloorForView(3840, 3744);
    const SOLSolidMenuProfile::FVrFloor twoEyes = SOLSolidMenuProfile::VrFloorForView(7680, 3744);
    const SOLSolidMenuProfile::FVrFloor none = SOLSolidMenuProfile::VrFloorForView(0, 0);
    TestTrue(TEXT("all under one eye's line"), MeetsVrFrameLine(8.0, 5.0, 7.0, oneEye));
    TestTrue(TEXT("all exactly at one eye's 10.0"), MeetsVrFrameLine(10.0, 10.0, 10.0, oneEye));
    TestFalse(TEXT("the frame just over 10.0"), MeetsVrFrameLine(10.01, 5.0, 7.0, oneEye));
    TestFalse(TEXT("the game thread just over 10.0"), MeetsVrFrameLine(8.0, 10.01, 7.0, oneEye));
    TestFalse(TEXT("the GPU just over 10.0"), MeetsVrFrameLine(8.0, 5.0, 10.01, oneEye));
    TestTrue(TEXT("all at two eyes' 9.5"), MeetsVrFrameLine(9.5, 9.5, 9.5, twoEyes));
    TestFalse(TEXT("9.8 ms meets one eye, not two"), MeetsVrFrameLine(9.8, 5.0, 7.0, twoEyes));
    TestTrue(TEXT("9.8 ms meets one eye"), MeetsVrFrameLine(9.8, 5.0, 7.0, oneEye));
    TestFalse(TEXT("no floor, no line"), MeetsVrFrameLine(1.0, 1.0, 1.0, none));

    // Not read (0 or -1), NaN and infinity never meet the line, in any of the three places
    const double bad[] = { 0.0, -1.0, SOLIDPROFILETEST_NAN, SOLIDPROFILETEST_INF, -SOLIDPROFILETEST_INF };
    for (const double value : bad)
    {
        TestFalse(FString::Printf(TEXT("frame %f"), value), MeetsVrFrameLine(value, 5.0, 7.0, oneEye));
        TestFalse(FString::Printf(TEXT("game %f"), value), MeetsVrFrameLine(8.0, value, 7.0, oneEye));
        TestFalse(FString::Printf(TEXT("GPU %f"), value), MeetsVrFrameLine(8.0, 5.0, value, oneEye));
    }
    return true;
}

#endif
