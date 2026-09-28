/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

using UnrealBuildTool;

public class SOLTest : ModuleRules
{
    public SOLTest(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Module-root-relative includes ("Universe/SOLKepler.h"), per Docs/STYLE_GUIDE.md section 14.2
        PublicIncludePaths.Add(ModuleDirectory);

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "EngineSettings",
            "EnhancedInput",
            "InputCore",
        });
    }
}
