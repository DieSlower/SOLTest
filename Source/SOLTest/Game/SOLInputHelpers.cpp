/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Game/SOLInputHelpers.h"

#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputTriggers.h"

//////////////////////////////////////////////////////////////////////////
// Creates an input action of the given value type, owned by outer
UInputAction* SOLInput::CreateAction(UObject* outer, const TCHAR* name, const EInputActionValueType valueType)
{
    UInputAction* action = NewObject<UInputAction>(outer, name);
    action->ValueType = valueType;
    return action;
}

//////////////////////////////////////////////////////////////////////////
// Maps a key to an action, optionally swizzling and negating the axis, and returns the mapping
FEnhancedActionKeyMapping& SOLInput::MapAxisKey(UInputMappingContext* context, UInputAction* action, const FKey& key,
    UObject* outer, const bool bSwizzle, const EInputAxisSwizzle order, const bool bNegate)
{
    FEnhancedActionKeyMapping& mapping = context->MapKey(action, key);
    if (bSwizzle)
    {
        UInputModifierSwizzleAxis* swizzle = NewObject<UInputModifierSwizzleAxis>(outer);
        swizzle->Order = order;
        mapping.Modifiers.Add(swizzle);
    }
    if (bNegate)
    {
        mapping.Modifiers.Add(NewObject<UInputModifierNegate>(outer));
    }
    return mapping;
}

//////////////////////////////////////////////////////////////////////////
// Maps a key to an action that triggers once per press
void SOLInput::MapPressedKey(UInputMappingContext* context, UInputAction* action, const FKey& key, UObject* outer)
{
    context->MapKey(action, key).Triggers.Add(NewObject<UInputTriggerPressed>(outer));
}

//////////////////////////////////////////////////////////////////////////
// Adds a mapping context to a local player's Enhanced Input subsystem; returns true on success
bool SOLInput::AddMappingContext(ULocalPlayer* localPlayer, UInputMappingContext* context, const int32 priority)
{
    UEnhancedInputLocalPlayerSubsystem* inputSubsystem = localPlayer != nullptr
        ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(localPlayer) : nullptr;
    if (inputSubsystem == nullptr || context == nullptr)
    {
        return false;
    }
    inputSubsystem->AddMappingContext(context, priority);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Removes a mapping context from a local player's Enhanced Input subsystem, if both exist
void SOLInput::RemoveMappingContext(ULocalPlayer* localPlayer, UInputMappingContext* context)
{
    UEnhancedInputLocalPlayerSubsystem* inputSubsystem = localPlayer != nullptr
        ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(localPlayer) : nullptr;
    if (inputSubsystem != nullptr && context != nullptr)
    {
        inputSubsystem->RemoveMappingContext(context);
    }
}
