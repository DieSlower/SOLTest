/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "InputActionValue.h"
#include "InputModifiers.h"

class ULocalPlayer;
class UInputAction;
class UInputMappingContext;
struct FEnhancedActionKeyMapping;
struct FKey;

// Helpers for building Enhanced Input actions and mapping contexts in C++ (no editor assets)
namespace SOLInput
{
    // Creates an input action of the given value type, owned by outer
    UInputAction* CreateAction(UObject* outer, const TCHAR* name, EInputActionValueType valueType);

    // Maps a key to an action, optionally swizzling and negating the axis, and returns the mapping
    FEnhancedActionKeyMapping& MapAxisKey(UInputMappingContext* context, UInputAction* action, const FKey& key,
        UObject* outer, bool bSwizzle, EInputAxisSwizzle order, bool bNegate);

    // Maps a key to an action that triggers once per press
    void MapPressedKey(UInputMappingContext* context, UInputAction* action, const FKey& key, UObject* outer);

    // Maps a key to an action that triggers once per press while the chord action triggers (e.g. Shift held); Enhanced
    // Input then blocks other mappings of that key in the same context while the chord is held
    void MapChordedKey(UInputMappingContext* context, UInputAction* action, const FKey& key,
        const UInputAction* chordAction, UObject* outer);

    // Adds a mapping context to a local player's Enhanced Input subsystem; returns true on success
    bool AddMappingContext(ULocalPlayer* localPlayer, UInputMappingContext* context, int32 priority);

    // Removes a mapping context from a local player's Enhanced Input subsystem, if both exist
    void RemoveMappingContext(ULocalPlayer* localPlayer, UInputMappingContext* context);
}
