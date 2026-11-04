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
// Maps a key to an action that triggers once per press while the chord action triggers
void SOLInput::MapChordedKey(UInputMappingContext* context, UInputAction* action, const FKey& key,
    const UInputAction* chordAction, UObject* outer)
{
    FEnhancedActionKeyMapping& mapping = context->MapKey(action, key);
    UInputTriggerChordAction* chord = NewObject<UInputTriggerChordAction>(outer);
    chord->ChordAction = chordAction;
    mapping.Triggers.Add(chord);
    mapping.Triggers.Add(NewObject<UInputTriggerPressed>(outer));
}

//////////////////////////////////////////////////////////////////////////
// Gives the plain mappings of these keys an explicit chord blocker on blockerAction and moves them to the context's end
int32 SOLInput::BlockPlainMappingsUnderChord(UInputMappingContext* context, const TArray<FKey>& keys,
    const UInputAction* blockerAction, UObject* outer)
{
    if (context == nullptr || blockerAction == nullptr || keys.IsEmpty())
    {
        return 0;
    }

    // Split copies of the mappings into the kept ones and the plain ones to block (a blocker is a chord trigger too,
    // so a second call finds nothing to block)
    TArray<FEnhancedActionKeyMapping> keptMappings;
    TArray<FEnhancedActionKeyMapping> plainMappings;
    for (const FEnhancedActionKeyMapping& mapping : context->GetMappings())
    {
        const bool bChorded = mapping.Triggers.ContainsByPredicate([](const UInputTrigger* trigger)
        {
            return Cast<const UInputTriggerChordAction>(trigger) != nullptr;
        });
        if (mapping.Action != nullptr && !bChorded && keys.Contains(mapping.Key))
        {
            plainMappings.Add(mapping);
        }
        else
        {
            keptMappings.Add(mapping);
        }
    }
    if (plainMappings.IsEmpty())
    {
        return 0;
    }

    // Rebuild the context in two passes: the kept mappings in their order, then every blocked plain mapping last with
    // the blocker (its own triggers and modifiers kept). UnmapKey swaps the last mapping into the hole, so unmapping
    // and re-adding one at a time could move a re-added mapping ahead of a chorded one. The engine adds a Down trigger
    // to a mapping without an explicit trigger when it injects its own blockers, as for any blocked plain mapping
    context->UnmapAll();
    for (const FEnhancedActionKeyMapping& kept : keptMappings)
    {
        context->MapKey(kept.Action, kept.Key) = kept;
    }
    for (const FEnhancedActionKeyMapping& plain : plainMappings)
    {
        FEnhancedActionKeyMapping& mapping = context->MapKey(plain.Action, plain.Key);
        mapping = plain;
        UInputTriggerChordBlocker* blocker = NewObject<UInputTriggerChordBlocker>(outer);
        blocker->ChordAction = blockerAction;
        mapping.Triggers.Add(blocker);
    }
    return plainMappings.Num();
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
