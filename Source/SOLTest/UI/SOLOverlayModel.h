/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "SOLConstants.h"

#include "CoreMinimal.h"

// Pure-logic overlay model (SDD 22 section 2.3, SDD 34 section 4): the data-driven table of overlay toggles the Overlay
// Menu (Ctrl+O) shows, and the player's overlay profiles. A profile is a bit mask over the table (bit i = row i
// checked) plus its own master O state and Alt+O orbit-lines switch. Up to SOL::Overlay::MAX_PROFILES profiles exist:
// profile 0 (shown as "Profile 1") is always enabled, profiles 1-4 are enabled independently, and the active profile
// is always an enabled one. The master O of the active profile turns every row on and off together.
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
        Lanes,              // NPC lane lines (issue #38, SDD 38 D17)
        Darkening,          // Distance darkening of far icons
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

    // The player's overlay choices (SDD 34 section 4.2): one row mask per profile and, per profile, one bit in each of
    // the enabled, used, master and orbit-lines sets (bit p = profile p)
    struct FOverlayPrefs
    {
        uint32 Masks[SOL::Overlay::MAX_PROFILES] = {};  // Rows per profile (bit i = row i checked)
        uint8 EnabledProfiles = 0x01;                   // Profile enabled; bit 0 (Profile 1) always set
        uint8 UsedProfiles = 0x01;                      // Profile has content (enabled at least once, D12)
        uint8 MasterOnProfiles = static_cast<uint8>(SOL::Overlay::ALL_PROFILES_MASK);    // Master O state (D4, D10)
        uint8 OrbitLinesProfiles = static_cast<uint8>(SOL::Overlay::ALL_PROFILES_MASK);  // Alt+O orbit lines (D18)
        int32 ActiveProfile = 0;                        // Always an enabled profile
    };

    // Returns whether a per-profile bit set (FOverlayPrefs' enabled, used, master and orbit-lines sets) holds a
    // profile's bit (profile in range)
    SOLTEST_API bool HasProfileBit(uint8 bits, int32 profile);

    // Sets or clears a profile's bit in a per-profile bit set (profile in range)
    SOLTEST_API void SetProfileBit(uint8& bits, int32 profile, bool bOn);

    // Returns a row's definition (index clamped into range)
    SOLTEST_API const FOverlayDef& GetDef(int32 overlay);

    // Returns the row with this id (case-insensitive), or INDEX_NONE
    SOLTEST_API int32 FindById(const FString& id);

    // Returns the mask with every row's bit set (the valid bits)
    SOLTEST_API uint32 AllBitsMask();

    // Returns the default mask: every row whose bDefaultOn is true
    SOLTEST_API uint32 DefaultMask();

    // Returns the default prefs: every mask the default, only profile 0 enabled and used, every master and orbit-lines
    // switch on, profile 0 active
    SOLTEST_API FOverlayPrefs MakeDefaultPrefs();

    // Returns whether a profile is enabled: always for profile 0, the enabled bit for 1-4, false out of range
    SOLTEST_API bool IsProfileEnabled(const FOverlayPrefs& prefs, int32 profile);

    // Returns whether a profile has content: always for profile 0 and any enabled profile, the used bit for a disabled
    // profile 1-4, false out of range
    SOLTEST_API bool IsProfileUsed(const FOverlayPrefs& prefs, int32 profile);

    // Enables or disables profile 1-4 and returns whether anything changed (profile 0 and out of range: no change).
    // The first enable copies profile 0's mask, master and orbit-lines bits into it; a re-enable keeps the profile's
    // own content (D12). Disabling keeps the content and moves an active profile back to 0
    SOLTEST_API bool SetProfileEnabled(FOverlayPrefs& prefs, int32 profile, bool bEnabled);

    // Makes an enabled profile active and returns whether the active profile changed (a disabled or out-of-range
    // profile leaves it unchanged)
    SOLTEST_API bool SetActiveProfile(FOverlayPrefs& prefs, int32 profile);

    // Makes the next enabled profile after the active one active (wrapping to 0) and returns it
    SOLTEST_API int32 CycleActiveProfile(FOverlayPrefs& prefs);

    // Returns the active profile's master O state
    SOLTEST_API bool IsMasterOn(const FOverlayPrefs& prefs);

    // Sets the active profile's master O state
    SOLTEST_API void SetMasterOn(FOverlayPrefs& prefs, bool bOn);

    // Flips the active profile's master O state and returns the new state
    SOLTEST_API bool ToggleMaster(FOverlayPrefs& prefs);

    // Returns the active profile's Alt+O orbit-lines switch
    SOLTEST_API bool AreOrbitLinesOn(const FOverlayPrefs& prefs);

    // Sets the active profile's Alt+O orbit-lines switch
    SOLTEST_API void SetOrbitLinesOn(FOverlayPrefs& prefs, bool bOn);

    // Flips the active profile's Alt+O orbit-lines switch and returns the new state
    SOLTEST_API bool ToggleOrbitLines(FOverlayPrefs& prefs);

    // Returns whether a row is checked in the active profile (false for an out-of-range row)
    SOLTEST_API bool IsEnabled(const FOverlayPrefs& prefs, EOverlay overlay);

    // Checks or unchecks a row in the active profile (out-of-range rows are ignored)
    SOLTEST_API void SetEnabled(FOverlayPrefs& prefs, EOverlay overlay, bool bEnabled);

    // Flips a row in the active profile and returns its new state
    SOLTEST_API bool Toggle(FOverlayPrefs& prefs, EOverlay overlay);

    // Returns whether an overlay shows now: its row is checked in the active profile, that profile's master (O) is on
    // and its parent row (if any) is checked. Turning the master off and on again therefore restores exactly what
    // showed before (nothing is lost)
    SOLTEST_API bool IsShown(const FOverlayPrefs& prefs, EOverlay overlay);

    // Returns whether a row is one of the four orbit-line rows
    SOLTEST_API bool IsOrbitOverlay(EOverlay overlay);

    // Returns whether an orbit category's lines show: the active profile's orbit-lines switch (Alt+O) is on and the row
    // is shown (IsShown); false for a row that is not an orbit row
    SOLTEST_API bool IsOrbitShown(const FOverlayPrefs& prefs, EOverlay overlay);

    // Returns the prefs made valid: the profile bit sets limited to the five profiles with profile 0 enabled and used
    // and every enabled profile used, the active profile clamped and moved to 0 when it is not enabled, every mask
    // limited to AllBitsMask(), and every unused slot reset to DefaultMask() with its master and orbit bits on (a used
    // but disabled slot keeps its content)
    SOLTEST_API FOverlayPrefs Sanitized(const FOverlayPrefs& prefs);

    // Returns whether two prefs are equal after sanitizing: the enabled, used, master and orbit bits, the active
    // profile and every mask (an unused slot's content is the default after sanitizing, so it never differs)
    SOLTEST_API bool AreEqual(const FOverlayPrefs& a, const FOverlayPrefs& b);

    // Returns the HUD profile tag's alpha elapsedS seconds after a cycle: 1 up to SOL::Overlay::PROFILE_TAG_SHOW_S,
    // then a linear fall to 0 over SOL::Overlay::PROFILE_TAG_FADE_S; 0 after that and for a negative or non-finite time
    SOLTEST_API float ProfileTagAlpha(double elapsedS);

    // Returns a profile mask as saved data: every row id mapped to whether it is checked
    SOLTEST_API TMap<FString, bool> ToSavedProfile(uint32 mask);

    // Returns the mask of saved data: ids are matched case-insensitively, unknown ids are ignored and rows missing
    // from the data take their default
    SOLTEST_API uint32 FromSavedProfile(const TMap<FString, bool>& saved);
}
