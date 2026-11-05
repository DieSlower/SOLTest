/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLBuildVersion.h"
#include "Menu/SOLMenuText.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #46 part 46d build version (Docs/SDDs/46-solid-menus.md): the "0.1.YYMM.<commit count>" version
// string, its YYMM and its main menu chin line, written against the Menu/SOLBuildVersion.h contract before the
// production code existed. The build-time values (BuildYYMM, BuildCommitCount) are only checked for shape, never for
// a particular date or count, so the tests stay deterministic on any machine and any day
namespace
{
    constexpr int32 BUILDVERSIONTEST_MIN_YYMM = 1;         // MakeYYMM's smallest result (year 00, month 1)
    constexpr int32 BUILDVERSIONTEST_MAX_YYMM = 9912;      // MakeYYMM's largest result (year 99, month 12)
    constexpr int32 BUILDVERSIONTEST_YYMM_DIGITS = 4;      // The version's YYMM field is always four digits
    constexpr const TCHAR* BUILDVERSIONTEST_PREFIX = TEXT("0.1.");

    // Counts failures of a sweep and keeps the first one's description (avoids flooding the log)
    struct FSOLBuildVersionSweepLog
    {
        int32 Failures = 0;
        FString First;

        // Records a failure, keeping the first description
        void Fail(const FString& what)
        {
            if (Failures == 0)
            {
                First = what;
            }
            ++Failures;
        }

        // Reports the sweep as one assertion
        void Report(FAutomationTestBase& test, const TCHAR* what) const
        {
            test.TestEqual(FString::Printf(TEXT("%s (first failure: %s)"), what, *First), Failures, 0);
        }
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns whether every character of text in [start, end) is a decimal digit (false for an empty range)
    bool BuildVersionTestAllDigits(const FString& text, const int32 start, const int32 end)
    {
        if (start >= end || start < 0 || end > text.Len())
        {
            return false;
        }
        for (int32 index = start; index < end; ++index)
        {
            if (!FChar::IsDigit(text[index]))
            {
                return false;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a version has the shape 0.1.DDDD.D+ (prefix, four digits, a dot, one or more digits, nothing
    // else), checked character by character
    bool BuildVersionTestIsVersionShape(const FString& version)
    {
        const FString prefix(BUILDVERSIONTEST_PREFIX);
        if (!version.StartsWith(prefix, ESearchCase::CaseSensitive))
        {
            return false;
        }

        // Four YYMM digits, a dot, then the commit count running to the end
        const int32 yymmStart = prefix.Len();
        const int32 yymmEnd = yymmStart + BUILDVERSIONTEST_YYMM_DIGITS;
        if (!BuildVersionTestAllDigits(version, yymmStart, yymmEnd))
        {
            return false;
        }
        if (version.Len() <= yymmEnd || version[yymmEnd] != TEXT('.'))
        {
            return false;
        }
        return BuildVersionTestAllDigits(version, yymmEnd + 1, version.Len());
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the version's YYMM field (the four characters after the prefix), or an empty string when too short
    FString BuildVersionTestYYMMField(const FString& version)
    {
        const int32 start = FString(BUILDVERSIONTEST_PREFIX).Len();
        if (version.Len() < start + BUILDVERSIONTEST_YYMM_DIGITS)
        {
            return FString();
        }
        return version.Mid(start, BUILDVERSIONTEST_YYMM_DIGITS);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the expected MakeYYMM per the contract: (year mod 100) x 100 + month, a negative year reads YY 0, the
    // month clamped to [1, 12]
    int32 BuildVersionTestExpectedYYMM(const int32 year, const int32 month)
    {
        const int32 yy = year < 0 ? 0 : year % 100;
        return yy * 100 + FMath::Clamp(month, 1, 12);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a line holds a line break (LF or CR)
    bool BuildVersionTestHasLineBreak(const FString& line)
    {
        return line.Contains(TEXT("\n"), ESearchCase::CaseSensitive)
            || line.Contains(TEXT("\r"), ESearchCase::CaseSensitive);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a chin line: starts with "v" + version, then the studio name after the version, then engineVersion
    // after the studio name (skipped when empty), and no line break
    void BuildVersionTestCheckLine(FAutomationTestBase& test, const FString& label, const FString& line,
        const FString& version, const FString& engineVersion)
    {
        const FString lead = TEXT("v") + version;
        test.TestTrue(FString::Printf(TEXT("%s: '%s' starts with '%s'"), *label, *line, *lead),
            line.StartsWith(lead, ESearchCase::CaseSensitive));

        // The studio name comes after the version
        const FString studio(SOLMenuText::STUDIO_NAME_SHORT);
        const int32 studioAt = line.Find(studio, ESearchCase::CaseSensitive, ESearchDir::FromStart, lead.Len());
        test.TestTrue(FString::Printf(TEXT("%s: '%s' holds the studio name after the version"), *label, *line),
            studioAt >= lead.Len());

        // The engine version comes after the studio name
        if (!engineVersion.IsEmpty() && studioAt != INDEX_NONE)
        {
            const int32 engineFrom = studioAt + studio.Len();
            const int32 engineAt = line.Find(engineVersion, ESearchCase::CaseSensitive, ESearchDir::FromStart,
                engineFrom);
            test.TestTrue(FString::Printf(TEXT("%s: '%s' holds the engine version '%s' after the studio name"),
                *label, *line, *engineVersion), engineAt >= engineFrom);
        }

        test.TestFalse(FString::Printf(TEXT("%s: '%s' has no line break"), *label, *line),
            BuildVersionTestHasLineBreak(line));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// MakeYYMM
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBuildVersionMakeYYMMFieldsTest, "SOLTest.BuildVersion.MakeYYMM.FieldOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The year's last two digits come first, then the month: 2026-11 is 2611, 2027-01 is 2701
bool FSOLBuildVersionMakeYYMMFieldsTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("2026-11"), SOLBuildVersion::MakeYYMM(2026, 11), 2611);
    TestEqual(TEXT("2026-12"), SOLBuildVersion::MakeYYMM(2026, 12), 2612);
    TestEqual(TEXT("2027-01"), SOLBuildVersion::MakeYYMM(2027, 1), 2701);
    TestEqual(TEXT("2026-01"), SOLBuildVersion::MakeYYMM(2026, 1), 2601);
    TestEqual(TEXT("2099-12: the largest"), SOLBuildVersion::MakeYYMM(2099, 12), BUILDVERSIONTEST_MAX_YYMM);
    TestEqual(TEXT("1999-12"), SOLBuildVersion::MakeYYMM(1999, 12), 9912);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBuildVersionMakeYYMMYearTest, "SOLTest.BuildVersion.MakeYYMM.YearModulo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The year reads mod 100: 2009-03 is 903 (no leading zero in the number), 2100 is YY 0, a negative year is YY 0
bool FSOLBuildVersionMakeYYMMYearTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("2009-03"), SOLBuildVersion::MakeYYMM(2009, 3), 903);
    TestEqual(TEXT("2000-01: the smallest"), SOLBuildVersion::MakeYYMM(2000, 1), BUILDVERSIONTEST_MIN_YYMM);
    TestEqual(TEXT("2100-05: YY 0"), SOLBuildVersion::MakeYYMM(2100, 5), 5);
    TestEqual(TEXT("2100-12: YY 0"), SOLBuildVersion::MakeYYMM(2100, 12), 12);
    TestEqual(TEXT("year 0"), SOLBuildVersion::MakeYYMM(0, 7), 7);
    TestEqual(TEXT("year 26 (already two digits)"), SOLBuildVersion::MakeYYMM(26, 11), 2611);
    TestEqual(TEXT("year 12026"), SOLBuildVersion::MakeYYMM(12026, 11), 2611);
    TestEqual(TEXT("year -1: YY 0"), SOLBuildVersion::MakeYYMM(-1, 6), 6);
    TestEqual(TEXT("year -2026: YY 0"), SOLBuildVersion::MakeYYMM(-2026, 11), 11);
    TestEqual(TEXT("lowest int32 year: YY 0"), SOLBuildVersion::MakeYYMM(TNumericLimits<int32>::Lowest(), 3), 3);
    TestEqual(TEXT("largest int32 year"), SOLBuildVersion::MakeYYMM(TNumericLimits<int32>::Max(), 3),
        (TNumericLimits<int32>::Max() % 100) * 100 + 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBuildVersionMakeYYMMMonthTest, "SOLTest.BuildVersion.MakeYYMM.MonthClamp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The month clamps to [1, 12] on both sides: 0 reads 1, 13 reads 12, -5 reads 1
bool FSOLBuildVersionMakeYYMMMonthTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("month 0 reads 1"), SOLBuildVersion::MakeYYMM(2026, 0), 2601);
    TestEqual(TEXT("month -5 reads 1"), SOLBuildVersion::MakeYYMM(2026, -5), 2601);
    TestEqual(TEXT("month 13 reads 12"), SOLBuildVersion::MakeYYMM(2026, 13), 2612);
    TestEqual(TEXT("month 100 reads 12"), SOLBuildVersion::MakeYYMM(2026, 100), 2612);
    TestEqual(TEXT("lowest int32 month reads 1"), SOLBuildVersion::MakeYYMM(2026, TNumericLimits<int32>::Lowest()),
        2601);
    TestEqual(TEXT("largest int32 month reads 12"), SOLBuildVersion::MakeYYMM(2026, TNumericLimits<int32>::Max()),
        2612);
    TestEqual(TEXT("2100 with month 0: the smallest"), SOLBuildVersion::MakeYYMM(2100, 0), BUILDVERSIONTEST_MIN_YYMM);
    TestEqual(TEXT("negative year and month: the smallest"), SOLBuildVersion::MakeYYMM(-7, -7),
        BUILDVERSIONTEST_MIN_YYMM);
    TestEqual(TEXT("2099 with month 13: the largest"), SOLBuildVersion::MakeYYMM(2099, 13), BUILDVERSIONTEST_MAX_YYMM);

    // Every month in range keeps its value
    for (int32 month = 1; month <= 12; ++month)
    {
        TestEqual(FString::Printf(TEXT("2026 month %d"), month), SOLBuildVersion::MakeYYMM(2026, month),
            2600 + month);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBuildVersionMakeYYMMSweepTest, "SOLTest.BuildVersion.MakeYYMM.RangeSweep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Over a wide sweep of years and months the result matches the contract, lies in [1, 9912], month part in [1, 12]
bool FSOLBuildVersionMakeYYMMSweepTest::RunTest(const FString& /*parameters*/)
{
    FSOLBuildVersionSweepLog log;
    for (int32 year = -3000; year <= 3000; year += 7)
    {
        for (int32 month = -3; month <= 15; ++month)
        {
            const int32 value = SOLBuildVersion::MakeYYMM(year, month);
            const int32 expected = BuildVersionTestExpectedYYMM(year, month);
            const bool bRange = value >= BUILDVERSIONTEST_MIN_YYMM && value <= BUILDVERSIONTEST_MAX_YYMM;
            const bool bMonth = value % 100 >= 1 && value % 100 <= 12;
            if (value != expected || !bRange || !bMonth)
            {
                log.Fail(FString::Printf(TEXT("year %d month %d: %d (expected %d)"), year, month, value, expected));
            }
        }
    }
    log.Report(*this, TEXT("MakeYYMM over years -3000..3000 and months -3..15"));
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// FormatVersion
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBuildVersionFormatExamplesTest, "SOLTest.BuildVersion.Format.Examples",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The contract's examples: "0.1." + YYMM padded to four digits + "." + commit count
bool FSOLBuildVersionFormatExamplesTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("(2611, 412)"), SOLBuildVersion::FormatVersion(2611, 412), FString(TEXT("0.1.2611.412")));
    TestEqual(TEXT("(903, 0)"), SOLBuildVersion::FormatVersion(903, 0), FString(TEXT("0.1.0903.0")));
    TestEqual(TEXT("(2701, 1)"), SOLBuildVersion::FormatVersion(2701, 1), FString(TEXT("0.1.2701.1")));
    TestEqual(TEXT("(2611, -3): negative count reads 0"), SOLBuildVersion::FormatVersion(2611, -3),
        FString(TEXT("0.1.2611.0")));
    TestEqual(TEXT("(12345, 7): YYMM clamps to 9999"), SOLBuildVersion::FormatVersion(12345, 7),
        FString(TEXT("0.1.9999.7")));
    TestEqual(TEXT("(-1, 5): YYMM clamps to 0000"), SOLBuildVersion::FormatVersion(-1, 5),
        FString(TEXT("0.1.0000.5")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBuildVersionFormatPaddingTest, "SOLTest.BuildVersion.Format.ZeroPadding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The YYMM field is zero padded to four digits (2009-03 is 0903); the commit count is never padded
bool FSOLBuildVersionFormatPaddingTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("2009-03 through MakeYYMM"), SOLBuildVersion::FormatVersion(SOLBuildVersion::MakeYYMM(2009, 3), 12),
        FString(TEXT("0.1.0903.12")));
    TestEqual(TEXT("2100-05 through MakeYYMM"), SOLBuildVersion::FormatVersion(SOLBuildVersion::MakeYYMM(2100, 5), 3),
        FString(TEXT("0.1.0005.3")));
    TestEqual(TEXT("yymm 0"), SOLBuildVersion::FormatVersion(0, 0), FString(TEXT("0.1.0000.0")));
    TestEqual(TEXT("yymm 1"), SOLBuildVersion::FormatVersion(1, 9), FString(TEXT("0.1.0001.9")));
    TestEqual(TEXT("yymm 99"), SOLBuildVersion::FormatVersion(99, 10), FString(TEXT("0.1.0099.10")));
    TestEqual(TEXT("yymm 999"), SOLBuildVersion::FormatVersion(999, 100), FString(TEXT("0.1.0999.100")));
    TestEqual(TEXT("yymm 1000"), SOLBuildVersion::FormatVersion(1000, 5), FString(TEXT("0.1.1000.5")));
    TestEqual(TEXT("yymm 9999"), SOLBuildVersion::FormatVersion(9999, 5), FString(TEXT("0.1.9999.5")));
    TestEqual(TEXT("count 10 is not padded"), SOLBuildVersion::FormatVersion(2611, 10), FString(TEXT("0.1.2611.10")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBuildVersionFormatClampTest, "SOLTest.BuildVersion.Format.Clamps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// YYMM clamps to [0, 9999] and a negative commit count reads 0, even at the int32 limits
bool FSOLBuildVersionFormatClampTest::RunTest(const FString& /*parameters*/)
{
    const int32 lowest = TNumericLimits<int32>::Lowest();
    const int32 largest = TNumericLimits<int32>::Max();
    TestEqual(TEXT("yymm 10000 clamps"), SOLBuildVersion::FormatVersion(10000, 1), FString(TEXT("0.1.9999.1")));
    TestEqual(TEXT("largest int32 yymm clamps"), SOLBuildVersion::FormatVersion(largest, 1),
        FString(TEXT("0.1.9999.1")));
    TestEqual(TEXT("yymm -9999 clamps"), SOLBuildVersion::FormatVersion(-9999, 1), FString(TEXT("0.1.0000.1")));
    TestEqual(TEXT("lowest int32 yymm clamps"), SOLBuildVersion::FormatVersion(lowest, 1),
        FString(TEXT("0.1.0000.1")));
    TestEqual(TEXT("count -1 reads 0"), SOLBuildVersion::FormatVersion(2611, -1), FString(TEXT("0.1.2611.0")));
    TestEqual(TEXT("lowest int32 count reads 0"), SOLBuildVersion::FormatVersion(2611, lowest),
        FString(TEXT("0.1.2611.0")));
    TestEqual(TEXT("both out of range"), SOLBuildVersion::FormatVersion(lowest, lowest), FString(TEXT("0.1.0000.0")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBuildVersionFormatLargeCountTest, "SOLTest.BuildVersion.Format.LargeCount",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Large commit counts are written in full, never truncated or clamped
bool FSOLBuildVersionFormatLargeCountTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("count 123456"), SOLBuildVersion::FormatVersion(2611, 123456), FString(TEXT("0.1.2611.123456")));
    TestEqual(TEXT("count 10000"), SOLBuildVersion::FormatVersion(2611, 10000), FString(TEXT("0.1.2611.10000")));
    TestEqual(TEXT("largest int32 count"), SOLBuildVersion::FormatVersion(2611, TNumericLimits<int32>::Max()),
        FString(TEXT("0.1.2611.2147483647")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBuildVersionFormatFourDigitsTest,
    "SOLTest.BuildVersion.Format.YYMMAlwaysFourDigits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// For any yymm (in range or not) the YYMM field is exactly four digits holding the clamped value, and the whole string
// has the 0.1.DDDD.D+ shape
bool FSOLBuildVersionFormatFourDigitsTest::RunTest(const FString& /*parameters*/)
{
    FSOLBuildVersionSweepLog log;
    for (int32 yymm = -50; yymm <= 10050; ++yymm)
    {
        const FString version = SOLBuildVersion::FormatVersion(yymm, 7);
        const FString field = BuildVersionTestYYMMField(version);
        const FString expectedField = FString::Printf(TEXT("%04d"), FMath::Clamp(yymm, 0, 9999));
        const bool bShape = BuildVersionTestIsVersionShape(version);
        const bool bTail = version.EndsWith(TEXT(".7"), ESearchCase::CaseSensitive);
        if (!bShape || !bTail || field != expectedField)
        {
            log.Fail(FString::Printf(TEXT("yymm %d: '%s' (expected YYMM field '%s')"), yymm, *version,
                *expectedField));
        }
    }
    log.Report(*this, TEXT("four-digit YYMM field over yymm -50..10050"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBuildVersionShapeHelperTest, "SOLTest.BuildVersion.Format.ShapeCheckSelfTest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The character-based shape check used by these tests accepts 0.1.DDDD.D+ and rejects near misses (guards the guard)
bool FSOLBuildVersionShapeHelperTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("0.1.2611.412"), BuildVersionTestIsVersionShape(TEXT("0.1.2611.412")));
    TestTrue(TEXT("0.1.0000.0"), BuildVersionTestIsVersionShape(TEXT("0.1.0000.0")));
    TestFalse(TEXT("three-digit YYMM"), BuildVersionTestIsVersionShape(TEXT("0.1.903.0")));
    TestFalse(TEXT("five-digit YYMM"), BuildVersionTestIsVersionShape(TEXT("0.1.12345.0")));
    TestFalse(TEXT("no count"), BuildVersionTestIsVersionShape(TEXT("0.1.2611.")));
    TestFalse(TEXT("no count or dot"), BuildVersionTestIsVersionShape(TEXT("0.1.2611")));
    TestFalse(TEXT("negative count"), BuildVersionTestIsVersionShape(TEXT("0.1.2611.-3")));
    TestFalse(TEXT("trailing text"), BuildVersionTestIsVersionShape(TEXT("0.1.2611.4a")));
    TestFalse(TEXT("leading v"), BuildVersionTestIsVersionShape(TEXT("v0.1.2611.4")));
    TestFalse(TEXT("wrong prefix"), BuildVersionTestIsVersionShape(TEXT("0.2.2611.4")));
    TestFalse(TEXT("empty"), BuildVersionTestIsVersionShape(FString()));
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Build-time values and the current version
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBuildVersionBuildValuesTest, "SOLTest.BuildVersion.Build.ValuesInRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// BuildYYMM is a valid YYMM (month part 1..12, value 1..9912) and BuildCommitCount is not negative (0 when git failed)
bool FSOLBuildVersionBuildValuesTest::RunTest(const FString& /*parameters*/)
{
    const int32 yymm = SOLBuildVersion::BuildYYMM();
    TestTrue(FString::Printf(TEXT("BuildYYMM %d in [1, 9912]"), yymm),
        yymm >= BUILDVERSIONTEST_MIN_YYMM && yymm <= BUILDVERSIONTEST_MAX_YYMM);
    TestTrue(FString::Printf(TEXT("BuildYYMM %d month part in [1, 12]"), yymm), yymm % 100 >= 1 && yymm % 100 <= 12);
    TestEqual(TEXT("BuildYYMM is a MakeYYMM fixed point"),
        SOLBuildVersion::MakeYYMM(2000 + yymm / 100, yymm % 100), yymm);

    const int32 count = SOLBuildVersion::BuildCommitCount();
    TestTrue(FString::Printf(TEXT("BuildCommitCount %d >= 0"), count), count >= 0);

    TestEqual(TEXT("BuildYYMM is stable"), SOLBuildVersion::BuildYYMM(), yymm);
    TestEqual(TEXT("BuildCommitCount is stable"), SOLBuildVersion::BuildCommitCount(), count);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBuildVersionCurrentTest, "SOLTest.BuildVersion.Build.CurrentVersion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// CurrentVersion is exactly FormatVersion(BuildYYMM(), BuildCommitCount()) and has the 0.1.DDDD.D+ shape
bool FSOLBuildVersionCurrentTest::RunTest(const FString& /*parameters*/)
{
    const FString current = SOLBuildVersion::CurrentVersion();
    const int32 yymm = SOLBuildVersion::BuildYYMM();
    const int32 count = SOLBuildVersion::BuildCommitCount();
    TestEqual(TEXT("CurrentVersion matches FormatVersion of the build values"), current,
        SOLBuildVersion::FormatVersion(yymm, count));
    TestTrue(FString::Printf(TEXT("'%s' has the 0.1.DDDD.D+ shape"), *current),
        BuildVersionTestIsVersionShape(current));
    TestEqual(TEXT("YYMM field is the build YYMM, four digits"), BuildVersionTestYYMMField(current),
        FString::Printf(TEXT("%04d"), yymm));
    TestTrue(TEXT("ends with the build commit count"),
        current.EndsWith(TEXT(".") + FString::FromInt(count), ESearchCase::CaseSensitive));
    TestEqual(TEXT("CurrentVersion is stable"), SOLBuildVersion::CurrentVersion(), current);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// FormatVersionLine (main menu chin recess)
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBuildVersionLineOrderTest, "SOLTest.BuildVersion.Line.Order",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The chin line starts with "v" + version, then holds the studio name, then the engine version, on one line
bool FSOLBuildVersionLineOrderTest::RunTest(const FString& /*parameters*/)
{
    const FString version(TEXT("0.1.2611.412"));
    const FString engine(TEXT("5.8.0"));
    BuildVersionTestCheckLine(*this, TEXT("plain"), SOLBuildVersion::FormatVersionLine(version, engine), version,
        engine);

    // A long engine version string with build metadata is kept whole and still comes last
    const FString longEngine(TEXT("5.8.0-41234567+++UE5+Release-5.8"));
    BuildVersionTestCheckLine(*this, TEXT("long engine version"),
        SOLBuildVersion::FormatVersionLine(version, longEngine), version, longEngine);

    // The live build's version
    const FString current = SOLBuildVersion::CurrentVersion();
    BuildVersionTestCheckLine(*this, TEXT("current version"), SOLBuildVersion::FormatVersionLine(current, engine),
        current, engine);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBuildVersionLineEmptyEngineTest, "SOLTest.BuildVersion.Line.EmptyEngineVersion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// An empty engine version still yields "v" + version followed by the studio name, on one line
bool FSOLBuildVersionLineEmptyEngineTest::RunTest(const FString& /*parameters*/)
{
    const FString version(TEXT("0.1.0903.0"));
    const FString line = SOLBuildVersion::FormatVersionLine(version, FString());
    BuildVersionTestCheckLine(*this, TEXT("empty engine version"), line, version, FString());
    TestFalse(TEXT("the line is not just the version"), line == TEXT("v") + version);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBuildVersionLineExactTest, "SOLTest.BuildVersion.Line.Exact",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The whole chin line for a given version and engine string: the SOLMenuText formats with every placeholder filled
// (no "{n}" left), pinned both as the literal text and as the format constants filled by hand
bool FSOLBuildVersionLineExactTest::RunTest(const FString& /*parameters*/)
{
    const FString version(TEXT("0.1.2611.135"));
    const FString engine(TEXT("5.8.3"));
    const FString line = SOLBuildVersion::FormatVersionLine(version, engine);
    TestEqual(TEXT("the full line, literal"), line,
        FString(TEXT("v0.1.2611.135  ·  ARS LLC  ·  UE 5.8.3")));
    const FString filled = FString(SOLMenuText::MAIN_MENU_VERSION_FORMAT).Replace(TEXT("{0}"), *version)
        .Replace(TEXT("{1}"), SOLMenuText::STUDIO_NAME_SHORT).Replace(TEXT("{2}"), *engine);
    TestEqual(TEXT("the full line, the format constant filled"), line, filled);
    TestFalse(TEXT("no placeholder left"), line.Contains(TEXT("{")) || line.Contains(TEXT("}")));

    // The form without an engine version
    const FString noEngine = SOLBuildVersion::FormatVersionLine(version, FString());
    TestEqual(TEXT("the line without an engine version, literal"), noEngine,
        FString(TEXT("v0.1.2611.135  ·  ARS LLC")));
    TestEqual(TEXT("the line without an engine version, the format constant filled"), noEngine,
        FString(SOLMenuText::MAIN_MENU_VERSION_NO_ENGINE_FORMAT).Replace(TEXT("{0}"), *version)
        .Replace(TEXT("{1}"), SOLMenuText::STUDIO_NAME_SHORT));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
