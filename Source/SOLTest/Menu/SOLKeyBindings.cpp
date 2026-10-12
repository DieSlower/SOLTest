/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLKeyBindings.h"

#include "Menu/SOLBindingLabel.h"
#include "Menu/SOLMenuText.h"

#include "InputCoreTypes.h"

namespace
{
    static_assert(UE_ARRAY_COUNT(SOLMenuText::KEY_BINDING_TEXTS) == SOLKeyBindings::BINDING_COUNT,
        "one area and label per binding");

    //////////////////////////////////////////////////////////////////////////
    // Returns a binding's definition: its id and default key, with its shown area and label from SOLMenuText
    constexpr SOLKeyBindings::FBindingDef KeyBindingDef(const SOLKeyBindings::EBinding binding,
        const TCHAR* id, const TCHAR* defaultKey)
    {
        return { id, SOLMenuText::KEY_BINDING_TEXTS[static_cast<int32>(binding)].Area,
            SOLMenuText::KEY_BINDING_TEXTS[static_cast<int32>(binding)].Label, defaultKey };
    }

    // Every rebindable action, in EBinding order. Defaults match the ship pawn's original hardcoded mapping and
    // GAME_MECHANICS.md section 1 (keep all three in sync)
    constexpr SOLKeyBindings::FBindingDef KEY_BINDING_DEFS[] =
    {
        KeyBindingDef(SOLKeyBindings::EBinding::ThrustForward, TEXT("ThrustForward"), TEXT("W")),
        KeyBindingDef(SOLKeyBindings::EBinding::ThrustBackward, TEXT("ThrustBackward"), TEXT("S")),
        KeyBindingDef(SOLKeyBindings::EBinding::StrafeLeft, TEXT("StrafeLeft"), TEXT("A")),
        KeyBindingDef(SOLKeyBindings::EBinding::StrafeRight, TEXT("StrafeRight"), TEXT("D")),
        KeyBindingDef(SOLKeyBindings::EBinding::ThrustUp, TEXT("ThrustUp"), TEXT("SpaceBar")),
        KeyBindingDef(SOLKeyBindings::EBinding::ThrustDown, TEXT("ThrustDown"), TEXT("LeftControl")),
        KeyBindingDef(SOLKeyBindings::EBinding::RollLeft, TEXT("RollLeft"), TEXT("Q")),
        KeyBindingDef(SOLKeyBindings::EBinding::RollRight, TEXT("RollRight"), TEXT("E")),
        KeyBindingDef(SOLKeyBindings::EBinding::Boost, TEXT("Boost"), TEXT("LeftShift")),
        KeyBindingDef(SOLKeyBindings::EBinding::ToggleAssist, TEXT("ToggleAssist"), TEXT("Tab")),
        KeyBindingDef(SOLKeyBindings::EBinding::FreeLook, TEXT("FreeLook"), TEXT("LeftAlt")),
        KeyBindingDef(SOLKeyBindings::EBinding::RecenterStick, TEXT("RecenterStick"), TEXT("MiddleMouseButton")),
        KeyBindingDef(SOLKeyBindings::EBinding::SpeedPanel, TEXT("SpeedPanel"), TEXT("F3")),
        KeyBindingDef(SOLKeyBindings::EBinding::SelectTarget, TEXT("SelectTarget"), TEXT("T")),
        KeyBindingDef(SOLKeyBindings::EBinding::NextTarget, TEXT("NextTarget"), TEXT("R")),
        KeyBindingDef(SOLKeyBindings::EBinding::PreviousTarget, TEXT("PreviousTarget"), TEXT("F")),
        KeyBindingDef(SOLKeyBindings::EBinding::ClearTarget, TEXT("ClearTarget"), TEXT("X")),
        KeyBindingDef(SOLKeyBindings::EBinding::MatchLock, TEXT("MatchLock"), TEXT("M")),
        KeyBindingDef(SOLKeyBindings::EBinding::SurfaceLock, TEXT("SurfaceLock"), TEXT("L")),
        KeyBindingDef(SOLKeyBindings::EBinding::Fire, TEXT("Fire"), TEXT("LeftMouseButton")),
        KeyBindingDef(SOLKeyBindings::EBinding::DropTarget, TEXT("DropTarget"), TEXT("G")),
        KeyBindingDef(SOLKeyBindings::EBinding::WarpDown, TEXT("WarpDown"), TEXT("LeftBracket")),
        KeyBindingDef(SOLKeyBindings::EBinding::WarpUp, TEXT("WarpUp"), TEXT("RightBracket")),
        KeyBindingDef(SOLKeyBindings::EBinding::WarpReset, TEXT("WarpReset"), TEXT("BackSpace")),
        KeyBindingDef(SOLKeyBindings::EBinding::ToggleOrbitLines, TEXT("ToggleOrbitLines"), TEXT("O")),
        KeyBindingDef(SOLKeyBindings::EBinding::RadarZoomIn, TEXT("RadarZoomIn"), TEXT("Hyphen")),
        KeyBindingDef(SOLKeyBindings::EBinding::RadarZoomOut, TEXT("RadarZoomOut"), TEXT("Equals")),
        KeyBindingDef(SOLKeyBindings::EBinding::RadarAuto, TEXT("RadarAuto"), TEXT("Home")),
        KeyBindingDef(SOLKeyBindings::EBinding::ToggleMap, TEXT("ToggleMap"), TEXT("J")),
    };
    static_assert(UE_ARRAY_COUNT(KEY_BINDING_DEFS) == SOLKeyBindings::BINDING_COUNT, "one definition per binding");

    // Keys fixed to actions that are not rebindable: Esc pauses, the mouse steers, the wheel steps the speed cap and
    // P is reserved for the predicted path
    const TCHAR* const KEY_BINDING_RESERVED[] =
    {
        TEXT("Escape"), TEXT("Mouse2D"), TEXT("MouseX"), TEXT("MouseY"), TEXT("MouseWheelAxis"),
        TEXT("MouseScrollUp"), TEXT("MouseScrollDown"), TEXT("P"),
    };

    // The jump map's own fixed keys (ASOLShipPawn::MapShipKeys): pick, orbit and pan drags, the pan / height modifier,
    // clear and jump. The map toggle is mapped in the map's context too, so it may not take any of them
    const TCHAR* const KEY_BINDING_MAP_FIXED[] =
    {
        TEXT("LeftMouseButton"), TEXT("RightMouseButton"), TEXT("MiddleMouseButton"), TEXT("LeftShift"),
        TEXT("RightShift"), TEXT("X"), TEXT("Enter"),
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a binding index is inside the table
    bool KeyBindingIsValid(const int32 binding)
    {
        return binding >= 0 && binding < SOLKeyBindings::BINDING_COUNT;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a key name is in a table (case-insensitive)
    bool KeyBindingIsListed(const FString& keyName, const TArrayView<const TCHAR* const> table)
    {
        for (const TCHAR* listed : table)
        {
            if (keyName.Equals(listed, ESearchCase::IgnoreCase))
            {
                return true;
            }
        }
        return false;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns a binding's definition (index clamped into range)
const SOLKeyBindings::FBindingDef& SOLKeyBindings::GetDef(const int32 binding)
{
    return KEY_BINDING_DEFS[FMath::Clamp(binding, 0, BINDING_COUNT - 1)];
}

//////////////////////////////////////////////////////////////////////////
// Returns the binding with this id, or INDEX_NONE
int32 SOLKeyBindings::FindById(const FString& id)
{
    for (int32 binding = 0; binding < BINDING_COUNT; ++binding)
    {
        if (id.Equals(KEY_BINDING_DEFS[binding].Id, ESearchCase::IgnoreCase))
        {
            return binding;
        }
    }
    return INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a key is fixed to a non-rebindable action
bool SOLKeyBindings::IsReservedKey(const FString& keyName)
{
    return KeyBindingIsListed(keyName, MakeArrayView(KEY_BINDING_RESERVED));
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a key is one of the jump map's own fixed keys
bool SOLKeyBindings::IsMapFixedKey(const FString& keyName)
{
    return KeyBindingIsListed(keyName, MakeArrayView(KEY_BINDING_MAP_FIXED));
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a key name is a real engine key that can be pressed (not an axis)
bool SOLKeyBindings::IsPressableKey(const FString& keyName)
{
    if (keyName.IsEmpty())
    {
        return false;
    }
    const FKey key(*keyName);
    return key.IsValid() && !key.IsAxis1D() && !key.IsAxis2D() && !key.IsAxis3D();
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a rebindable action may take this key
bool SOLKeyBindings::CanBindKey(const FString& keyName)
{
    return SOLBindingLabel::IsRebindable(keyName) && !IsReservedKey(keyName) && IsPressableKey(keyName);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether one binding may take this key (CanBindKey, and the map toggle refuses the map's fixed keys)
bool SOLKeyBindings::CanBindKeyTo(const int32 binding, const FString& keyName)
{
    if (!KeyBindingIsValid(binding) || !CanBindKey(keyName))
    {
        return false;
    }
    return binding != static_cast<int32>(EBinding::ToggleMap) || !IsMapFixedKey(keyName);
}

//////////////////////////////////////////////////////////////////////////
// Returns the text with every '{<BindingId>}' replaced by that binding's key label
FString SOLKeyBindings::FormatKeyText(const TCHAR* format, const TArray<FString>& keys)
{
    FStringFormatNamedArguments arguments;
    arguments.Reserve(BINDING_COUNT);
    for (int32 binding = 0; binding < BINDING_COUNT; ++binding)
    {
        arguments.Add(KEY_BINDING_DEFS[binding].Id, SOLBindingLabel::KeyDisplayName(keys.IsValidIndex(binding)
            ? keys[binding] : FString()));
    }
    return FString::Format(format, arguments);
}

//////////////////////////////////////////////////////////////////////////
// Returns the default key of every binding
TArray<FString> SOLKeyBindings::DefaultKeys()
{
    TArray<FString> keys;
    keys.Reserve(BINDING_COUNT);
    for (const FBindingDef& def : KEY_BINDING_DEFS)
    {
        keys.Add(def.DefaultKey);
    }
    return keys;
}

//////////////////////////////////////////////////////////////////////////
// Returns the default keys with the valid overrides applied
TArray<FString> SOLKeyBindings::ResolveKeys(const TMap<FString, FString>& overrides)
{
    TArray<FString> keys = DefaultKeys();
    for (const TPair<FString, FString>& entry : overrides)
    {
        const int32 binding = FindById(entry.Key);
        if (binding != INDEX_NONE && CanBindKeyTo(binding, entry.Value))
        {
            keys[binding] = entry.Value;
        }
    }
    return keys;
}

//////////////////////////////////////////////////////////////////////////
// Returns the overrides that differ from the defaults
TMap<FString, FString> SOLKeyBindings::MakeOverrides(const TArray<FString>& keys)
{
    TMap<FString, FString> overrides;
    const int32 count = FMath::Min(keys.Num(), BINDING_COUNT);
    for (int32 binding = 0; binding < count; ++binding)
    {
        if (!keys[binding].Equals(KEY_BINDING_DEFS[binding].DefaultKey, ESearchCase::IgnoreCase))
        {
            overrides.Add(KEY_BINDING_DEFS[binding].Id, keys[binding]);
        }
    }
    return overrides;
}

//////////////////////////////////////////////////////////////////////////
// Returns another binding that uses the key, or INDEX_NONE
int32 SOLKeyBindings::FindConflict(const TArray<FString>& keys, const int32 binding, const FString& keyName)
{
    if (keyName.IsEmpty())
    {
        return INDEX_NONE;
    }
    for (int32 other = 0; other < keys.Num(); ++other)
    {
        if (other != binding && keys[other].Equals(keyName, ESearchCase::IgnoreCase))
        {
            return other;
        }
    }
    return INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a binding shares its key with another binding
bool SOLKeyBindings::HasConflict(const TArray<FString>& keys, const int32 binding)
{
    return keys.IsValidIndex(binding) && FindConflict(keys, binding, keys[binding]) != INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Binds a key to a binding and reports another binding that also uses it
SOLKeyBindings::ERebindResult SOLKeyBindings::Rebind(TArray<FString>& keys, const int32 binding,
    const FString& keyName, int32& outConflict)
{
    outConflict = INDEX_NONE;
    if (!keys.IsValidIndex(binding) || !CanBindKeyTo(binding, keyName))
    {
        return ERebindResult::Refused;
    }
    outConflict = FindConflict(keys, binding, keyName);
    if (keys[binding].Equals(keyName, ESearchCase::IgnoreCase))
    {
        return ERebindResult::Unchanged;
    }
    keys[binding] = keyName;
    return ERebindResult::Bound;
}
