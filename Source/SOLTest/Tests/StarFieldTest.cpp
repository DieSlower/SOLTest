/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "StarField/SOLStarFieldData.h"
#include "SOLConstants.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/TextureCube.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Content checks for the star-field assets (SDD 5, 4b). They run in the normal repo state, with CelestialVault NOT
// enabled, so they are the lasting guard that the copied plugin assets stand on their own (decision 5)
namespace
{
    // Content folder holding every star-field asset, including the CelestialVault copies
    const TCHAR* STAR_FIELD_CONTENT_PATH = TEXT("/Game/SOL/StarField");

    // Packages the import script creates (Tools/StarField/import_star_field.py); each must exist
    const TCHAR* EXPECTED_STAR_FIELD_PACKAGES[] = {
        TEXT("/Game/SOL/StarField/T_SOLStarFieldCube"),
        TEXT("/Game/SOL/StarField/DA_SOLStarField"),
        TEXT("/Game/SOL/StarField/CelestialVault/T_MilkyWay"),
        TEXT("/Game/SOL/StarField/CelestialVault/T_StarMask_Round"),
        TEXT("/Game/SOL/StarField/CelestialVault/MF_BillboardSizeByPixelUnits"),
        TEXT("/Game/SOL/StarField/CelestialVault/MF_ScalePlaneToMinScreenPixels"),
        TEXT("/Game/SOL/StarField/CelestialVault/MF_DirectionToLatLong"),
        TEXT("/Game/SOL/StarField/CelestialVault/M_Stars_EnergyConservative"),
        TEXT("/Game/SOL/StarField/CelestialVault/SM_Plane_FacingX"),
    };

    // Dependency prefixes meaning an asset still needs the plugin (content, its code module, or DaySequence's)
    const TCHAR* PLUGIN_LEAK_PREFIXES[] = {
        TEXT("/CelestialVault/"),
        TEXT("/Script/CelestialVault"),
        TEXT("/Script/DaySequence"),
    };

    // Unit-length tolerance for the baked float32 directions
    constexpr float STAR_DIRECTION_UNIT_TOLERANCE = 1.0e-5f;

    //////////////////////////////////////////////////////////////////////////
    // Returns every asset under the star-field folder after a synchronous registry scan of it
    TArray<FAssetData> ScanStarFieldAssets()
    {
        IAssetRegistry& registry = IAssetRegistry::GetChecked();
        registry.ScanPathsSynchronous({ FString(STAR_FIELD_CONTENT_PATH) }, true);
        TArray<FAssetData> assets;
        registry.GetAssetsByPath(FName(STAR_FIELD_CONTENT_PATH), assets, true);
        return assets;
    }

    //////////////////////////////////////////////////////////////////////////
    // Loads the star-field data asset from its central path, or returns nullptr
    USOLStarFieldData* LoadStarFieldData()
    {
        return LoadObject<USOLStarFieldData>(nullptr, SOL::Paths::STAR_FIELD_DATA);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLStarFieldNoPluginDependencyTest, "SOLTest.StarField.NoCelestialVaultDependency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every star-field package is known to the registry, records dependencies, none on the plugin, and loads
bool FSOLStarFieldNoPluginDependencyTest::RunTest(const FString& /*parameters*/)
{
    IAssetRegistry& registry = IAssetRegistry::GetChecked();
    const TArray<FAssetData> assets = ScanStarFieldAssets();

    // The expected packages are all present
    TSet<FName> packages;
    for (const FAssetData& asset : assets)
    {
        packages.Add(asset.PackageName);
    }
    for (const TCHAR* expected : EXPECTED_STAR_FIELD_PACKAGES)
    {
        TestTrue(FString::Printf(TEXT("Asset registry knows %s"), expected), packages.Contains(FName(expected)));
    }

    // Each package found: non-empty dependency list, no plugin dependency, and it loads
    for (const FAssetData& asset : assets)
    {
        const FString packageName = asset.PackageName.ToString();
        TArray<FName> dependencies;
        const bool bKnown = registry.GetDependencies(asset.PackageName, dependencies);
        TestTrue(FString::Printf(TEXT("%s has a dependency entry"), *packageName), bKnown);
        TestTrue(FString::Printf(TEXT("%s has a non-empty dependency list"), *packageName), dependencies.Num() > 0);
        for (const FName dependency : dependencies)
        {
            const FString dependencyName = dependency.ToString();
            for (const TCHAR* prefix : PLUGIN_LEAK_PREFIXES)
            {
                TestFalse(FString::Printf(TEXT("%s depends on %s"), *packageName, *dependencyName),
                    dependencyName.StartsWith(prefix));
            }
        }
        TestNotNull(FString::Printf(TEXT("%s loads"), *packageName), asset.GetAsset());
    }

    // The registry really records cross-asset references: the reference material uses the copied mask (re-pointed by
    // the import script), so an empty or stale dependency graph cannot pass the checks above unnoticed
    TArray<FName> materialDependencies;
    registry.GetDependencies(FName(TEXT("/Game/SOL/StarField/CelestialVault/M_Stars_EnergyConservative")),
        materialDependencies);
    TestTrue(TEXT("M_Stars_EnergyConservative depends on the copied T_StarMask_Round"),
        materialDependencies.Contains(FName(TEXT("/Game/SOL/StarField/CelestialVault/T_StarMask_Round"))));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLStarFieldBrightStarsTest, "SOLTest.StarField.BrightStarsValid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The data asset loads with stars whose directions are unit vectors, sorted brightest first
bool FSOLStarFieldBrightStarsTest::RunTest(const FString& /*parameters*/)
{
    const USOLStarFieldData* data = LoadStarFieldData();
    if (!TestNotNull(TEXT("STAR_FIELD_DATA loads as USOLStarFieldData"), data))
    {
        return false;
    }
    const TArray<FSOLBrightStar>& stars = data->BrightStars;
    TestTrue(TEXT("At least one bright star"), stars.Num() > 0);

    // Count violations instead of logging one error per star
    int32 nonUnitCount = 0;
    int32 outOfOrderCount = 0;
    for (int32 index = 0; index < stars.Num(); ++index)
    {
        if (FMath::Abs(stars[index].Direction.Size() - 1.0f) > STAR_DIRECTION_UNIT_TOLERANCE)
        {
            ++nonUnitCount;
        }
        if (index > 0 && stars[index].VisualMagnitude < stars[index - 1].VisualMagnitude)
        {
            ++outOfOrderCount;
        }
    }
    TestEqual(TEXT("Stars whose direction is not a unit vector"), nonUnitCount, 0);
    TestEqual(TEXT("Stars out of ascending-V order"), outOfOrderCount, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLStarFieldMetadataTest, "SOLTest.StarField.MetadataValid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The texel solid angle matches the face size and the license attribution is present
bool FSOLStarFieldMetadataTest::RunTest(const FString& /*parameters*/)
{
    const USOLStarFieldData* data = LoadStarFieldData();
    if (!TestNotNull(TEXT("STAR_FIELD_DATA loads as USOLStarFieldData"), data))
    {
        return false;
    }
    TestTrue(TEXT("CubeFaceSize is positive"), data->CubeFaceSize > 0);
    const double expectedSolidAngle = FMath::Square(2.0 / FMath::Max(data->CubeFaceSize, 1));
    TestEqual(TEXT("CubeTexelSolidAngleSr == (2 / CubeFaceSize)^2"), static_cast<double>(data->CubeTexelSolidAngleSr),
        expectedSolidAngle, expectedSolidAngle * 1.0e-6);
    TestFalse(TEXT("Attribution is non-empty"), data->Attribution.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLStarFieldCubeTest, "SOLTest.StarField.CubeSettings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The faint-star cubemap loads as a linear, BC6H-compressed TextureCube
bool FSOLStarFieldCubeTest::RunTest(const FString& /*parameters*/)
{
    const UTextureCube* cube = LoadObject<UTextureCube>(nullptr, SOL::Paths::STAR_FIELD_CUBE);
    if (!TestNotNull(TEXT("STAR_FIELD_CUBE loads as UTextureCube"), cube))
    {
        return false;
    }
    TestEqual(TEXT("Compression is HDR Compressed (BC6H)"), static_cast<int32>(cube->CompressionSettings.GetValue()),
        static_cast<int32>(TC_HDR_Compressed));
    TestFalse(TEXT("sRGB is off"), static_cast<bool>(cube->SRGB));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
