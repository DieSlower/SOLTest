/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLEngineSettingsSnapshot.h"

#include "SOLTest.h"

#include "GameFramework/GameUserSettings.h"
#include "UObject/UnrealType.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns one element of a property of an object as config text (as SaveConfig writes it), a zero value included
    FString EngineSnapshotExport(const FProperty& property, const UObject& object, const int32 index)
    {
        // ExportText_InContainer without a delta writes nothing for a zero value (it compares against zero), which
        // would not import back; the item form always writes the value
        FString text;
        property.ExportTextItem_Direct(text, property.ContainerPtrToValuePtr<void>(&object, index), nullptr, nullptr,
            PPF_None);
        return text;
    }
}

//////////////////////////////////////////////////////////////////////////
// Copies every config property of a settings object and the engine's scalability levels
FSOLEngineSettingsSnapshot FSOLEngineSettingsSnapshot::Capture(const UGameUserSettings& settings)
{
    FSOLEngineSettingsSnapshot snapshot;
    snapshot.SettingsClass = settings.GetClass();
    for (TFieldIterator<FProperty> it(settings.GetClass()); it; ++it)
    {
        const FProperty* property = *it;
        if (!property->HasAnyPropertyFlags(CPF_Config))
        {
            continue;
        }
        for (int32 index = 0; index < property->ArrayDim; ++index)
        {
            FValue& value = snapshot.Values.AddDefaulted_GetRef();
            value.Property = property->GetFName();
            value.Index = index;
            value.Text = EngineSnapshotExport(*property, settings, index);
        }
    }
    snapshot.QualityLevels = Scalability::GetQualityLevels();
    return snapshot;
}

//////////////////////////////////////////////////////////////////////////
// Writes the copied config properties back (no apply); returns how many differed
int32 FSOLEngineSettingsSnapshot::RestoreTo(UGameUserSettings& settings) const
{
    if (!IsCaptured() || settings.GetClass() != SettingsClass.Get())
    {
        return 0;
    }

    // Each element compared as config text first (case-sensitively: a string property may differ in case only), so an
    // unchanged one is left alone; an element whose text does not import is skipped and logged
    int32 changed = 0;
    for (const FValue& value : Values)
    {
        const FProperty* property = FindFProperty<FProperty>(settings.GetClass(), value.Property);
        if (property == nullptr || value.Index >= property->ArrayDim
            || EngineSnapshotExport(*property, settings, value.Index).Equals(value.Text, ESearchCase::CaseSensitive))
        {
            continue;
        }
        if (property->ImportText_Direct(*value.Text, property->ContainerPtrToValuePtr<void>(&settings, value.Index),
            &settings, PPF_None) == nullptr)
        {
            UE_LOG(LogSOL, Warning, TEXT("Engine settings snapshot: %s[%d] did not import back"),
                *value.Property.ToString(), value.Index);
            continue;
        }
        ++changed;
    }
    return changed;
}

//////////////////////////////////////////////////////////////////////////
// Sets and applies the engine's scalability levels back if they differ; returns whether they did
bool FSOLEngineSettingsSnapshot::RestoreQualityLevels() const
{
    if (!IsCaptured() || Scalability::GetQualityLevels() == QualityLevels)
    {
        return false;
    }
    Scalability::SetQualityLevels(QualityLevels);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns a copied property's text (element 0), or null
const FString* FSOLEngineSettingsSnapshot::FindValue(const FName property) const
{
    const FValue* found = Values.FindByPredicate([property](const FValue& value)
    {
        return value.Property == property && value.Index == 0;
    });
    return found != nullptr ? &found->Text : nullptr;
}
