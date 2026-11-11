/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Tools/SOLEditorAuthoring.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "UObject/UnrealType.h"

#if WITH_EDITOR
//////////////////////////////////////////////////////////////////////////
// Sets a string property on an object through reflection regardless of its edit flags, then notifies the object
bool USOLEditorAuthoring::SetObjectStringProperty(UObject* object, FName propertyName, const FString& value)
{
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
    object->PreEditChange(property);
    property->SetPropertyValue_InContainer(object, value);

    // The same notification a details-panel edit sends, so the owner (e.g. a PCG node) recompiles
    FPropertyChangedEvent changedEvent(property);
    object->PostEditChangeProperty(changedEvent);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Connects an expression's first output to a material's Customized UV input (0-7)
bool USOLEditorAuthoring::ConnectMaterialCustomizedUV(UMaterial* material, UMaterialExpression* expression,
    const int32 uvIndex)
{
    if (material == nullptr || expression == nullptr || uvIndex < 0 || uvIndex >= MP_NumCustomizedUVs)
    {
        return false;
    }
    FExpressionInput* input = material->GetExpressionInputForProperty(
        static_cast<EMaterialProperty>(MP_CustomizedUVs0 + uvIndex));
    if (input == nullptr)
    {
        return false;
    }
    material->Modify();
    input->Connect(0, expression);
    return true;
}
#endif
