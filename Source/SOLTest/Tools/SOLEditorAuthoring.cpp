/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Tools/SOLEditorAuthoring.h"

#include "UObject/UnrealType.h"

// Sets a string property on an object through reflection regardless of its edit flags, then notifies the object
bool USOLEditorAuthoring::SetObjectStringProperty(UObject* object, FName propertyName, const FString& value)
{
#if WITH_EDITOR
    if (object == nullptr)
    {
        return false;
    }

    FStrProperty* property = CastField<FStrProperty>(object->GetClass()->FindPropertyByName(propertyName));
    if (property == nullptr)
    {
        return false;
    }

    object->Modify();
    property->SetPropertyValue_InContainer(object, value);

    // The same notification a details-panel edit sends, so the owner (e.g. a PCG node) recompiles
    FPropertyChangedEvent changedEvent(property);
    object->PostEditChangeProperty(changedEvent);
    return true;
#else
    return false;
#endif
}
