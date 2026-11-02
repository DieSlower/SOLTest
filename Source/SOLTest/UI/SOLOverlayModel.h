/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "SOLConstants.h"

#include "CoreMinimal.h"

// Pure-logic overlay model (SDD 22 section 2.3): the data-driven table of overlay toggles the Overlay Menu (Ctrl+O)
// shows, which of them the master O key turns on and off together, and the per-profile choices saved in Settings.
// A profile is a bit mask over the table (bit i = row i checked). The struct holds up to SOL::Overlay::MAX_PROFILES
// profiles for issue #34; issue #22 uses one (profile 1 = the defaults: every row on except Asteroid Orbits).
namespace SOLOverlay
{
    // Overlay toggles in Overlay Menu order (also the bit index in a profile mask). Adding an overlay is one row here
    // and one in the table (SOLOverlayModel.cpp)
    enum class EOverlay : uint8
    {
        Icons,              // Every icon (the parent of the eight icon categories below)
        IconStars,
        IconPlanets,
        IconMoons,
        IconDwarfs,
        IconAsteroids,
        IconRings,
        IconShips,
        IconTargets,
        PlanetOrbits,
        AsteroidOrbits,
        MoonOrbits,
        OtherOrbits,        // Dwarf-planet orbits and the transient line of a target without a permanent line
        ShipsOnRadar,       // NPC ship contacts on the radar
        Darkening,          // Distance darkening of far icons (Shift+O)
        Count,
    };

    inline constexpr int32 OVERLAY_COUNT = static_cast<int32>(EOverlay::Count);

    // One table row: stable id (persisted), shown label, default (profile 1) and the row whose checkbox gates it
    // (INDEX_NONE for a top-level row; the icon categories have Icons as their parent)
    struct FOverlayDef
    {
        const TCHAR* Id;
        const TCHAR* Label;
        bool bDefaultOn;
        int32 Parent;
    };

    // The player's overlay choices: one mask per profile, how many profiles exist and which one is active
    struct FOverlayPrefs
    {
        uint32 Masks[SOL::Overlay::MAX_PROFILES] = {};
        int32 ProfileCount = 1;
        int32 ActiveProfile = 0;
    };

    // Returns a row's definition (index clamped into range)
    SOLTEST_API const FOverlayDef& GetDef(int32 overlay);

    // Returns the row with this id (case-insensitive), or INDEX_NONE
    SOLTEST_API int32 FindById(const FString& id);

    // Returns the mask with every row's bit set (the valid bits)
    SOLTEST_API uint32 AllBitsMask();

    // Returns the default mask: every row whose bDefaultOn is true
    SOLTEST_API uint32 DefaultMask();

    // Returns one profile holding the default mask (every unused profile slot holds the default mask too)
    SOLTEST_API FOverlayPrefs MakeDefaultPrefs();

    // Returns whether a row is checked in the active profile (false for an out-of-range row)
    SOLTEST_API bool IsEnabled(const FOverlayPrefs& prefs, EOverlay overlay);

    // Checks or unchecks a row in the active profile (out-of-range rows are ignored)
    SOLTEST_API void SetEnabled(FOverlayPrefs& prefs, EOverlay overlay, bool bEnabled);

    // Flips a row in the active profile and returns its new state
    SOLTEST_API bool Toggle(FOverlayPrefs& prefs, EOverlay overlay);

    // Returns whether an overlay shows now: the master (O) is on, its row is checked and its parent row (if any) is
    // checked. Turning the master off and on again therefore restores exactly what showed before (nothing is lost)
    SOLTEST_API bool IsShown(const FOverlayPrefs& prefs, bool bMasterOn, EOverlay overlay);

    // Returns whether a row is one of the four orbit-line rows
    SOLTEST_API bool IsOrbitOverlay(EOverlay overlay);

    // Returns whether an orbit category's lines show: IsShown and the orbit lines switch (Alt+O) is on; false for a
    // row that is not an orbit row
    SOLTEST_API bool IsOrbitShown(const FOverlayPrefs& prefs, bool bMasterOn, bool bOrbitLinesOn, EOverlay overlay);

    // Returns the prefs made valid: ProfileCount clamped to [1, MAX_PROFILES], ActiveProfile to [0, ProfileCount - 1],
    // every mask limited to AllBitsMask(), and every slot at or beyond ProfileCount reset to DefaultMask()
    SOLTEST_API FOverlayPrefs Sanitized(const FOverlayPrefs& prefs);

    // Returns whether two prefs are equal after sanitizing (slots beyond ProfileCount are ignored)
    SOLTEST_API bool AreEqual(const FOverlayPrefs& a, const FOverlayPrefs& b);

    // Returns a profile mask as saved data: every row id mapped to whether it is checked
    SOLTEST_API TMap<FString, bool> ToSavedProfile(uint32 mask);

    // Returns the mask of saved data: ids are matched case-insensitively, unknown ids are ignored and rows missing
    // from the data take their default
    SOLTEST_API uint32 FromSavedProfile(const TMap<FString, bool>& saved);
}
