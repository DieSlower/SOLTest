/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Pure-logic key labelling for the Controls screen (SDD 8): maps engine key names to short display text.
namespace SOLBindingLabel
{
    // Returns the display text for an engine key name ("Unbound" for an empty name)
    SOLTEST_API FString KeyDisplayName(const FString& keyName);

    // Returns the display names joined with "+" ("Unbound" for an empty list)
    SOLTEST_API FString JoinChord(const TArray<FString>& keyNames);

    // Returns whether the player may rebind to this key (not Escape, not unbound)
    SOLTEST_API bool IsRebindable(const FString& keyName);
}
