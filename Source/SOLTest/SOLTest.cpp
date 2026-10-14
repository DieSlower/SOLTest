/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "SOLTest.h"

#include "SOLConstants.h"

#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY(LogSOL);

// The game module: maps the project's Shaders/ folder so material Custom nodes can include its .ush files (SDD 16)
class FSOLTestModule final : public FDefaultGameModuleImpl
{
public:

    //////////////////////////////////////////////////////////////////////////
    // Maps SOL::SHADER_VIRTUAL_DIRECTORY to <Project>/Shaders before any project material compiles. The module stays in
    // the Default loading phase (it holds UCLASSes; PostConfigInit would load it before its UObject dependencies), which
    // is still before the project's materials load; a missing folder is only expected in a cooked build, whose shaders
    // are precompiled, so the editor warns about it
    virtual void StartupModule() override
    {
        const FString shaderDirectory = FPaths::Combine(FPaths::ProjectDir(), SOL::SHADER_SOURCE_FOLDER);
        if (!FPaths::DirectoryExists(shaderDirectory))
        {
#if WITH_EDITOR
            UE_LOG(LogSOL, Warning, TEXT("SOLTest: shader folder %s is missing; materials that include %s will not "
                "compile"), *shaderDirectory, SOL::SHADER_VIRTUAL_DIRECTORY);
#endif
            return;
        }
        if (!AllShaderSourceDirectoryMappings().Contains(SOL::SHADER_VIRTUAL_DIRECTORY))
        {
            AddShaderSourceDirectoryMapping(SOL::SHADER_VIRTUAL_DIRECTORY, shaderDirectory);
        }
    }
};

IMPLEMENT_PRIMARY_GAME_MODULE(FSOLTestModule, SOLTest, "SOLTest");
