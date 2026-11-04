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

    // Gives every plain mapping (no chord trigger) of one of these keys an explicit chord blocker on blockerAction, so
    // it stays silent while blockerAction triggers, and moves it to the end of the context. Enhanced Input sorts a
    // mapping with any chord trigger (a blocker included) among the chorded ones in mapping order and injects a
    // chorded mapping's blocker only into the same-key mappings after it, so the move keeps the plain mapping blocked
    // by every chorded mapping of its key (Alt, Ctrl, Right Shift) and keeps it from blocking them in turn. Call it
    // after the context's last MapKey; returns the number of mappings changed
    int32 BlockPlainMappingsUnderChord(UInputMappingContext* context, const TArray<FKey>& keys,
        const UInputAction* blockerAction, UObject* outer);

    // Adds a mapping context to a local player's Enhanced Input subsystem; returns true on success
    bool AddMappingContext(ULocalPlayer* localPlayer, UInputMappingContext* context, int32 priority);

    // Removes a mapping context from a local player's Enhanced Input subsystem, if both exist
    void RemoveMappingContext(ULocalPlayer* localPlayer, UInputMappingContext* context);
}
