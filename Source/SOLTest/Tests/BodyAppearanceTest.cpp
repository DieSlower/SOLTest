/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "SOLConstants.h"
#include "Visuals/SOLBodyVisuals.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for SOLBodyAppearance::FindBaseColor (Docs/SDDs/3-jump-map.md, section 9 "Colors"): the body material's
// linear ColorA from the appearance table, or false for a body without an appearance
namespace
{
    // Sentinel written before a lookup, to prove a failed lookup leaves the output untouched
    const FLinearColor BODY_APPEARANCE_SENTINEL(0.123f, 0.456f, 0.789f, 0.5f);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyAppearanceKnownColorsTest, "SOLTest.BodyAppearance.KnownBaseColors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The Sun and Earth return their table ColorA (Sun pale yellow-white, Earth deep blue) with alpha 1
bool FSOLBodyAppearanceKnownColorsTest::RunTest(const FString& /*parameters*/)
{
    FLinearColor sun = BODY_APPEARANCE_SENTINEL;
    TestTrue(TEXT("Sun has a base color"), SOLBodyAppearance::FindBaseColor(FName(SOL::BodyNames::SUN), sun));
    TestTrue(TEXT("Sun base color is (1.0, 0.9, 0.7, 1)"), sun == FLinearColor(1.0f, 0.9f, 0.7f, 1.0f));

    FLinearColor earth = BODY_APPEARANCE_SENTINEL;
    TestTrue(TEXT("Earth has a base color"), SOLBodyAppearance::FindBaseColor(FName(SOL::BodyNames::EARTH), earth));
    TestTrue(TEXT("Earth base color is (0.02, 0.1, 0.42, 1)"),
        earth == FLinearColor(0.02f, 0.1f, 0.42f, 1.0f));
    TestTrue(TEXT("Earth reads blue (blue is its brightest channel)"), earth.B > earth.R && earth.B > earth.G);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyAppearanceAllPlanetsTest, "SOLTest.BodyAppearance.EveryPlanetHasColor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every body the game ships with has a base color, each a valid non-black linear color with alpha 1
bool FSOLBodyAppearanceAllPlanetsTest::RunTest(const FString& /*parameters*/)
{
    const TCHAR* names[] = { SOL::BodyNames::SUN, TEXT("Mercury"), TEXT("Venus"), SOL::BodyNames::EARTH, TEXT("Mars"),
        TEXT("Jupiter"), TEXT("Saturn"), TEXT("Uranus"), TEXT("Neptune") };
    for (const TCHAR* name : names)
    {
        FLinearColor color = BODY_APPEARANCE_SENTINEL;
        const bool bFound = SOLBodyAppearance::FindBaseColor(FName(name), color);
        TestTrue(*FString::Printf(TEXT("%s has a base color"), name), bFound);
        TestFalse(*FString::Printf(TEXT("%s output was written"), name), color == BODY_APPEARANCE_SENTINEL);
        TestTrue(*FString::Printf(TEXT("%s color channels are in [0, 1]"), name), color.R >= 0.0f && color.R <= 1.0f
            && color.G >= 0.0f && color.G <= 1.0f && color.B >= 0.0f && color.B <= 1.0f);
        TestTrue(*FString::Printf(TEXT("%s color is not black"), name), FMath::Max3(color.R, color.G, color.B) > 0.0f);
        TestEqual(*FString::Printf(TEXT("%s alpha is 1"), name), color.A, 1.0f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyAppearanceUnknownTest, "SOLTest.BodyAppearance.UnknownBodyReturnsFalse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An unknown name, NAME_None or a near-miss spelling returns false and leaves the output untouched
bool FSOLBodyAppearanceUnknownTest::RunTest(const FString& /*parameters*/)
{
    const FName names[] = { FName(TEXT("Pluto")), NAME_None, FName(TEXT("Earth2")) };
    for (const FName& name : names)
    {
        FLinearColor color = BODY_APPEARANCE_SENTINEL;
        TestFalse(*FString::Printf(TEXT("%s has no base color"), *name.ToString()),
            SOLBodyAppearance::FindBaseColor(name, color));
        TestTrue(*FString::Printf(TEXT("%s leaves the output untouched"), *name.ToString()),
            color == BODY_APPEARANCE_SENTINEL);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
