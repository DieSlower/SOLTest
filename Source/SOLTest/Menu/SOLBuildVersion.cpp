/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLBuildVersion.h"

#include "Menu/SOLMenuText.h"

// Written by SOLTest.Build.cs into Intermediate/Generated (SDD 46 section 11.1); included here only. Without it (the
// rules could not write it) the build still compiles: the YYMM falls back to the compile date, the count to 0
#if __has_include("SOLBuildVersion.generated.h")
#include "SOLBuildVersion.generated.h"
#endif

namespace
{
    constexpr int32 BUILD_VERSION_MAX_YYMM = 9999;     // The YYMM field is four digits

    //////////////////////////////////////////////////////////////////////////
    // The month (1..12) of a __DATE__ string ("Mmm dd yyyy"); 1 when the name is not known
    constexpr int32 BuildVersionDateMonth(const char* date)
    {
        constexpr const char* MONTHS = "JanFebMarAprMayJunJulAugSepOctNovDec";
        for (int32 month = 0; month < 12; ++month)
        {
            if (date[0] == MONTHS[3 * month] && date[1] == MONTHS[3 * month + 1] && date[2] == MONTHS[3 * month + 2])
            {
                return month + 1;
            }
        }
        return 1;
    }

    //////////////////////////////////////////////////////////////////////////
    // The year of a __DATE__ string ("Mmm dd yyyy")
    constexpr int32 BuildVersionDateYear(const char* date)
    {
        return (date[7] - '0') * 1000 + (date[8] - '0') * 100 + (date[9] - '0') * 10 + (date[10] - '0');
    }
}

//////////////////////////////////////////////////////////////////////////
// The YYMM of a date: the year's last two digits (0 for a negative year), then the clamped month
int32 SOLBuildVersion::MakeYYMM(const int32 year, const int32 month)
{
    const int32 yy = year < 0 ? 0 : year % 100;
    return yy * 100 + FMath::Clamp(month, 1, 12);
}

//////////////////////////////////////////////////////////////////////////
// "0.1." + the clamped YYMM in four digits + "." + the commit count (never negative, never padded)
FString SOLBuildVersion::FormatVersion(const int32 yymm, const int32 commitCount)
{
    return FString::Printf(TEXT("0.1.%04d.%d"), FMath::Clamp(yymm, 0, BUILD_VERSION_MAX_YYMM),
        FMath::Max(commitCount, 0));
}

//////////////////////////////////////////////////////////////////////////
// The YYMM of the build, from the generated header (the compile date's without it)
int32 SOLBuildVersion::BuildYYMM()
{
#ifdef SOL_BUILD_YYMM
    return SOL_BUILD_YYMM;
#else
    return MakeYYMM(BuildVersionDateYear(__DATE__), BuildVersionDateMonth(__DATE__));
#endif
}

//////////////////////////////////////////////////////////////////////////
// The repository's commit count at the build, from the generated header (0 without it)
int32 SOLBuildVersion::BuildCommitCount()
{
#ifdef SOL_BUILD_COMMIT_COUNT
    return SOL_BUILD_COMMIT_COUNT;
#else
    return 0;
#endif
}

//////////////////////////////////////////////////////////////////////////
// The version of this build
FString SOLBuildVersion::CurrentVersion()
{
    return FormatVersion(BuildYYMM(), BuildCommitCount());
}

//////////////////////////////////////////////////////////////////////////
// The chin recess line: version, studio, engine (left out when empty)
FString SOLBuildVersion::FormatVersionLine(const FString& version, const FString& engineVersion)
{
    using namespace SOLMenuText;
    return engineVersion.IsEmpty()
        ? FString::Format(MAIN_MENU_VERSION_NO_ENGINE_FORMAT, { version, STUDIO_NAME_SHORT })
        : FString::Format(MAIN_MENU_VERSION_FORMAT, { version, STUDIO_NAME_SHORT, engineVersion });
}
