/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

/**
 * The build version shown in the main menu's chin recess (SDD 46 section 11.1, Q10): "0.1.YYMM.<commit count>", where
 * YYMM is the two-digit year and month of the build and the count is `git rev-list --count HEAD`. SOLTest.Build.cs
 * writes both values into a generated header when the module rules run (count 0 when git fails); only
 * SOLBuildVersion.cpp includes it, and builds without it (YYMM from the compile date, count 0). The formatting is pure,
 * so the tests cover it without a build.
 */
namespace SOLBuildVersion
{
    // The YYMM of a date: (year mod 100) x 100 + the month clamped to [1, 12]; a negative year reads YY 0
    SOLTEST_API int32 MakeYYMM(int32 year, int32 month);

    // "0.1." + yymm clamped to [0, 9999] and zero padded to four digits + "." + the commit count (a negative reads 0)
    SOLTEST_API FString FormatVersion(int32 yymm, int32 commitCount);

    // The YYMM of the build (from the generated header; the compile date's when it is missing)
    SOLTEST_API int32 BuildYYMM();

    // The repository's commit count at the build (from the generated header; 0 when git failed or it is missing)
    SOLTEST_API int32 BuildCommitCount();

    // FormatVersion(BuildYYMM(), BuildCommitCount())
    SOLTEST_API FString CurrentVersion();

    // The chin recess line: "v" + version, the studio name, then the engine version (left out when empty), one line
    // (SOLMenuText::MAIN_MENU_VERSION_FORMAT)
    SOLTEST_API FString FormatVersionLine(const FString& version, const FString& engineVersion);
}
