/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "HudEvents/SOLHudEventSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"

//////////////////////////////////////////////////////////////////////////
// Returns the world's bus subsystem, or nullptr
USOLHudEventSubsystem* USOLHudEventSubsystem::Get(const UObject* worldContext)
{
    const UWorld* world = GEngine != nullptr && worldContext != nullptr
        ? GEngine->GetWorldFromContextObject(worldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
    return world != nullptr ? world->GetSubsystem<USOLHudEventSubsystem>() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Exists in game and PIE worlds only
bool USOLHudEventSubsystem::DoesSupportWorldType(const EWorldType::Type worldType) const
{
    return worldType == EWorldType::Game || worldType == EWorldType::PIE;
}
