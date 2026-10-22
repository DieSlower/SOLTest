/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Pure-logic key rebinding (SDD 8, 8d): the table of rebindable ship actions with their default keys, the per-action
// key array (engine key names, one per binding) and the player's overrides of it, rebinding with conflict detection.
// The ship pawn builds its mapping context from the resolved array; USOLSettingsSubsystem persists the overrides.
namespace SOLKeyBindings
{
    // Rebindable ship actions, in Settings and Controls order (also the index into a key array)
    enum class EBinding : uint8
    {
        ThrustForward,
        ThrustBackward,
        StrafeLeft,
        StrafeRight,
        ThrustUp,
        ThrustDown,
        RollLeft,
        RollRight,
        Boost,
        ToggleAssist,
        StopSpin,
        FreeLook,
        RecenterStick,
        SpeedPanel,
        SelectTarget,
        NextTarget,
        PreviousTarget,
        ClearTarget,
        MatchLock,
        SurfaceLock,
        Fire,
        DropTarget,
        WarpDown,
        WarpUp,
        WarpReset,
        ToggleOrbitLines,
        RadarZoomIn,
        RadarZoomOut,
        RadarAuto,
        ToggleMap,
        Count,
    };

    inline constexpr int32 BINDING_COUNT = static_cast<int32>(EBinding::Count);

    // One rebindable action: its stable id (persisted), its area and label (shown) and its default engine key name
    struct FBindingDef
    {
        const TCHAR* Id;
        const TCHAR* Area;
        const TCHAR* Label;
        const TCHAR* DefaultKey;
    };

    // Outcome of a rebind request
    enum class ERebindResult : uint8
    {
        Bound,      // The binding now uses the key
        Unchanged,  // It already used the key
        Refused,    // The key cannot be bound (Escape, empty, unknown, an axis, a fixed ship key, or a jump map key for
                    // the map toggle)
    };

    // Returns a binding's definition (index clamped into range)
    SOLTEST_API const FBindingDef& GetDef(int32 binding);

    // Returns the binding with this id (case-insensitive), or INDEX_NONE
    SOLTEST_API int32 FindById(const FString& id);

    // Returns whether a key is fixed to an action that cannot be rebound (Escape pause, mouse look and wheel, the
    // reserved P, the F1 / F2 of the Ctrl hotkeys), so no rebindable action may take it
    SOLTEST_API bool IsReservedKey(const FString& keyName);

    // Returns whether a key press is the Ctrl+F1 menu hotkey (acts like Esc; SDD 11 section 3.9)
    SOLTEST_API bool IsMenuHotkey(const FString& keyName, bool bControlDown);

    // Returns whether a key press is the Ctrl+F2 NPC stats overlay hotkey
    SOLTEST_API bool IsStatsHotkey(const FString& keyName, bool bControlDown);

    // Returns whether a key is one of the jump map's own fixed keys (left, right and middle mouse, Left / Right Shift,
    // X, Enter), which the map toggle (mapped in the map's context too) may not take
    SOLTEST_API bool IsMapFixedKey(const FString& keyName);

    // Returns whether a key name is an engine key that can be pressed: known to the engine (FKey::IsValid) and not an
    // axis
    SOLTEST_API bool IsPressableKey(const FString& keyName);

    // Returns whether a rebindable action may take this key (SOLBindingLabel::IsRebindable, not reserved, pressable)
    SOLTEST_API bool CanBindKey(const FString& keyName);

    // Returns whether one binding may take this key: CanBindKey, and the map toggle also refuses the map's fixed keys
    // (false for an out-of-range binding)
    SOLTEST_API bool CanBindKeyTo(int32 binding, const FString& keyName);

    // Returns the text with every '{<BindingId>}' (e.g. "{SurfaceLock}") replaced by that binding's key label
    // (SOLBindingLabel) from the key array; a short array reads as unbound, other text is unchanged
    SOLTEST_API FString FormatKeyText(const TCHAR* format, const TArray<FString>& keys);

    // Returns the default key of every binding
    SOLTEST_API TArray<FString> DefaultKeys();

    // Returns the default keys with the overrides (id -> key name) applied; unknown ids and keys that binding cannot
    // take (CanBindKeyTo: unknown or axis key names from a corrupt save included) are ignored
    SOLTEST_API TArray<FString> ResolveKeys(const TMap<FString, FString>& overrides);

    // Returns the overrides that turn the defaults into these keys (only the bindings that differ from their default)
    SOLTEST_API TMap<FString, FString> MakeOverrides(const TArray<FString>& keys);

    // Returns another binding that uses the key (case-insensitive), or INDEX_NONE
    SOLTEST_API int32 FindConflict(const TArray<FString>& keys, int32 binding, const FString& keyName);

    // Returns whether a binding shares its key with another binding
    SOLTEST_API bool HasConflict(const TArray<FString>& keys, int32 binding);

    // Binds a key to a binding; outConflict is another binding that also uses the key (INDEX_NONE if none). A conflict
    // does not block the rebind: both keep the key until the player changes one, and both are flagged
    SOLTEST_API ERebindResult Rebind(TArray<FString>& keys, int32 binding, const FString& keyName, int32& outConflict);
}
