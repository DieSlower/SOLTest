/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "CoreMinimal.h"

#include "SOLEditorAuthoring.generated.h"

// Editor-only authoring helper for editor Python scripts (never called by gameplay code). Exists because some
// engine asset properties that authoring needs (e.g. a PCG Custom HLSL node's shader source) are plain
// UPROPERTY() with no edit/Blueprint flags, so Python's set_editor_property and the MCP toolsets cannot reach them.
UCLASS()
class USOLEditorAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Sets a string property on an object through reflection regardless of its edit flags, then fires
    // PostEditChangeProperty so the owner reacts as it would to a details-panel edit; returns false if not found
    UFUNCTION(BlueprintCallable, Category = "SOL|EditorAuthoring")
    static bool SetObjectStringProperty(UObject* object, FName propertyName, const FString& value);
};
