/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLOverlayModel.h"

namespace
{
    // Parent row of the eight icon categories
    constexpr int32 OVERLAY_ICONS_ROW = static_cast<int32>(SOLOverlay::EOverlay::Icons);

    // Every overlay row in EOverlay order (Overlay Menu order): id (persisted), label, default and parent row
    constexpr SOLOverlay::FOverlayDef OVERLAY_DEFS[] =
    {
        { TEXT("Icons"), TEXT("Icons"), true, INDEX_NONE },
        { TEXT("IconStars"), TEXT("Stars"), true, OVERLAY_ICONS_ROW },
        { TEXT("IconPlanets"), TEXT("Planets"), true, OVERLAY_ICONS_ROW },
        { TEXT("IconMoons"), TEXT("Moons"), true, OVERLAY_ICONS_ROW },
        { TEXT("IconDwarfs"), TEXT("Dwarf planets"), true, OVERLAY_ICONS_ROW },
        { TEXT("IconAsteroids"), TEXT("Asteroids"), true, OVERLAY_ICONS_ROW },
        { TEXT("IconRings"), TEXT("Rings"), true, OVERLAY_ICONS_ROW },
        { TEXT("IconShips"), TEXT("Ships"), true, OVERLAY_ICONS_ROW },
        { TEXT("IconTargets"), TEXT("Targets"), true, OVERLAY_ICONS_ROW },
        { TEXT("PlanetOrbits"), TEXT("Planet Orbits"), true, INDEX_NONE },
        { TEXT("AsteroidOrbits"), TEXT("Asteroid Orbits"), false, INDEX_NONE },
        { TEXT("MoonOrbits"), TEXT("Moon Orbits"), true, INDEX_NONE },
        { TEXT("OtherOrbits"), TEXT("Other Orbits"), true, INDEX_NONE },
        { TEXT("ShipsOnRadar"), TEXT("Ships on Radar"), true, INDEX_NONE },
        { TEXT("Lanes"), TEXT("Lanes"), true, INDEX_NONE },
        { TEXT("Darkening"), TEXT("Darkening"), true, INDEX_NONE },
    };
    static_assert(UE_ARRAY_COUNT(OVERLAY_DEFS) == SOLOverlay::OVERLAY_COUNT, "one definition per overlay");
    static_assert(SOLOverlay::OVERLAY_COUNT <= 32, "a profile mask holds 32 overlays");
    static_assert(SOL::Overlay::PROFILE_TAG_FADE_S > 0.0, "the profile tag fade divides by its duration");

    //////////////////////////////////////////////////////////////////////////
    // Returns whether an overlay index is inside the table
    bool OverlayIsValid(const int32 overlay)
    {
        return overlay >= 0 && overlay < SOLOverlay::OVERLAY_COUNT;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an overlay's bit in a profile mask
    uint32 OverlayBit(const int32 overlay)
    {
        return 1u << static_cast<uint32>(overlay);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the active profile clamped into the slot range (a corrupt index never reads out of bounds)
    int32 OverlayActiveSlot(const SOLOverlay::FOverlayPrefs& prefs)
    {
        return FMath::Clamp(prefs.ActiveProfile, 0, SOL::Overlay::MAX_PROFILES - 1);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a profile index is inside the slot range
    bool OverlayProfileIsValid(const int32 profile)
    {
        return profile >= 0 && profile < SOL::Overlay::MAX_PROFILES;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a profile's bit in a per-profile bit set (profile in range)
    uint8 OverlayProfileBit(const int32 profile)
    {
        return static_cast<uint8>(1u << static_cast<uint32>(profile));
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a per-profile bit set holds a profile's bit (profile in range)
bool SOLOverlay::HasProfileBit(const uint8 bits, const int32 profile)
{
    return (bits & OverlayProfileBit(profile)) != 0;
}

//////////////////////////////////////////////////////////////////////////
// Sets or clears a profile's bit in a per-profile bit set (profile in range)
void SOLOverlay::SetProfileBit(uint8& bits, const int32 profile, const bool bOn)
{
    bits = bOn ? static_cast<uint8>(bits | OverlayProfileBit(profile))
        : static_cast<uint8>(bits & ~OverlayProfileBit(profile));
}

//////////////////////////////////////////////////////////////////////////
// Returns a row's definition (index clamped into range)
const SOLOverlay::FOverlayDef& SOLOverlay::GetDef(const int32 overlay)
{
    return OVERLAY_DEFS[FMath::Clamp(overlay, 0, OVERLAY_COUNT - 1)];
}

//////////////////////////////////////////////////////////////////////////
// Returns the row with this id (case-insensitive), or INDEX_NONE
int32 SOLOverlay::FindById(const FString& id)
{
    for (int32 overlay = 0; overlay < OVERLAY_COUNT; ++overlay)
    {
        if (id.Equals(OVERLAY_DEFS[overlay].Id, ESearchCase::IgnoreCase))
        {
            return overlay;
        }
    }
    return INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Returns the mask with every row's bit set
uint32 SOLOverlay::AllBitsMask()
{
    return OVERLAY_COUNT >= 32 ? ~0u : (1u << OVERLAY_COUNT) - 1u;
}

//////////////////////////////////////////////////////////////////////////
// Returns the mask of every row that is on by default
uint32 SOLOverlay::DefaultMask()
{
    uint32 mask = 0;
    for (int32 overlay = 0; overlay < OVERLAY_COUNT; ++overlay)
    {
        mask |= OVERLAY_DEFS[overlay].bDefaultOn ? OverlayBit(overlay) : 0u;
    }
    return mask;
}

//////////////////////////////////////////////////////////////////////////
// Returns the default prefs (every slot holds the defaults)
SOLOverlay::FOverlayPrefs SOLOverlay::MakeDefaultPrefs()
{
    FOverlayPrefs prefs;
    for (uint32& mask : prefs.Masks)
    {
        mask = DefaultMask();
    }
    return prefs;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a profile is enabled: always profile 0, the enabled bit for 1-4, false out of range
bool SOLOverlay::IsProfileEnabled(const FOverlayPrefs& prefs, const int32 profile)
{
    return profile == 0 || (OverlayProfileIsValid(profile) && HasProfileBit(prefs.EnabledProfiles, profile));
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a profile has content: profile 0, any enabled profile, else the used bit; false out of range
bool SOLOverlay::IsProfileUsed(const FOverlayPrefs& prefs, const int32 profile)
{
    return IsProfileEnabled(prefs, profile)
        || (OverlayProfileIsValid(profile) && HasProfileBit(prefs.UsedProfiles, profile));
}

//////////////////////////////////////////////////////////////////////////
// Enables or disables profile 1-4 (first enable copies profile 0, re-enable keeps its content, disable keeps it too);
// returns whether anything changed
bool SOLOverlay::SetProfileEnabled(FOverlayPrefs& prefs, const int32 profile, const bool bEnabled)
{
    // Profile 0 is always enabled; an out-of-range profile does not exist
    if (profile <= 0 || !OverlayProfileIsValid(profile)
        || HasProfileBit(prefs.EnabledProfiles, profile) == bEnabled)
    {
        return false;
    }
    if (bEnabled)
    {
        // The first enable starts as a copy of profile 0 (D8); a re-enable restores the profile's own content (D12)
        if (!IsProfileUsed(prefs, profile))
        {
            prefs.Masks[profile] = prefs.Masks[0];
            SetProfileBit(prefs.MasterOnProfiles, profile, HasProfileBit(prefs.MasterOnProfiles, 0));
            SetProfileBit(prefs.OrbitLinesProfiles, profile, HasProfileBit(prefs.OrbitLinesProfiles, 0));
        }
        SetProfileBit(prefs.EnabledProfiles, profile, true);
        SetProfileBit(prefs.UsedProfiles, profile, true);
        return true;
    }

    // Disabling keeps the content for a later re-enable; the active profile falls back to profile 0
    SetProfileBit(prefs.EnabledProfiles, profile, false);
    SetProfileBit(prefs.UsedProfiles, profile, true);
    if (prefs.ActiveProfile == profile)
    {
        prefs.ActiveProfile = 0;
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Makes an enabled profile active; returns whether the active profile changed
bool SOLOverlay::SetActiveProfile(FOverlayPrefs& prefs, const int32 profile)
{
    if (!IsProfileEnabled(prefs, profile) || prefs.ActiveProfile == profile)
    {
        return false;
    }
    prefs.ActiveProfile = profile;
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Makes the next enabled profile after the active one active (wrapping to 0) and returns it
int32 SOLOverlay::CycleActiveProfile(FOverlayPrefs& prefs)
{
    // Profile 0 is always enabled, so the walk ends at the latest one full turn after the start
    const int32 start = OverlayActiveSlot(prefs);
    for (int32 step = 1; step <= SOL::Overlay::MAX_PROFILES; ++step)
    {
        const int32 candidate = (start + step) % SOL::Overlay::MAX_PROFILES;
        if (IsProfileEnabled(prefs, candidate))
        {
            prefs.ActiveProfile = candidate;
            return candidate;
        }
    }
    prefs.ActiveProfile = 0;
    return 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the active profile's master O state
bool SOLOverlay::IsMasterOn(const FOverlayPrefs& prefs)
{
    return HasProfileBit(prefs.MasterOnProfiles, OverlayActiveSlot(prefs));
}

//////////////////////////////////////////////////////////////////////////
// Sets the active profile's master O state
void SOLOverlay::SetMasterOn(FOverlayPrefs& prefs, const bool bOn)
{
    SetProfileBit(prefs.MasterOnProfiles, OverlayActiveSlot(prefs), bOn);
}

//////////////////////////////////////////////////////////////////////////
// Flips the active profile's master O state and returns the new state
bool SOLOverlay::ToggleMaster(FOverlayPrefs& prefs)
{
    SetMasterOn(prefs, !IsMasterOn(prefs));
    return IsMasterOn(prefs);
}

//////////////////////////////////////////////////////////////////////////
// Returns the active profile's Alt+O orbit-lines switch
bool SOLOverlay::AreOrbitLinesOn(const FOverlayPrefs& prefs)
{
    return HasProfileBit(prefs.OrbitLinesProfiles, OverlayActiveSlot(prefs));
}

//////////////////////////////////////////////////////////////////////////
// Sets the active profile's Alt+O orbit-lines switch
void SOLOverlay::SetOrbitLinesOn(FOverlayPrefs& prefs, const bool bOn)
{
    SetProfileBit(prefs.OrbitLinesProfiles, OverlayActiveSlot(prefs), bOn);
}

//////////////////////////////////////////////////////////////////////////
// Flips the active profile's Alt+O orbit-lines switch and returns the new state
bool SOLOverlay::ToggleOrbitLines(FOverlayPrefs& prefs)
{
    SetOrbitLinesOn(prefs, !AreOrbitLinesOn(prefs));
    return AreOrbitLinesOn(prefs);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a row is checked in the active profile
bool SOLOverlay::IsEnabled(const FOverlayPrefs& prefs, const EOverlay overlay)
{
    const int32 index = static_cast<int32>(overlay);
    return OverlayIsValid(index) && (prefs.Masks[OverlayActiveSlot(prefs)] & OverlayBit(index)) != 0;
}

//////////////////////////////////////////////////////////////////////////
// Checks or unchecks a row in the active profile
void SOLOverlay::SetEnabled(FOverlayPrefs& prefs, const EOverlay overlay, const bool bEnabled)
{
    const int32 index = static_cast<int32>(overlay);
    if (!OverlayIsValid(index))
    {
        return;
    }
    uint32& mask = prefs.Masks[OverlayActiveSlot(prefs)];
    mask = bEnabled ? mask | OverlayBit(index) : mask & ~OverlayBit(index);
}

//////////////////////////////////////////////////////////////////////////
// Flips a row in the active profile and returns its new state
bool SOLOverlay::Toggle(FOverlayPrefs& prefs, const EOverlay overlay)
{
    SetEnabled(prefs, overlay, !IsEnabled(prefs, overlay));
    return IsEnabled(prefs, overlay);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether an overlay shows now: its row checked, the active profile's master on and its parent row checked
bool SOLOverlay::IsShown(const FOverlayPrefs& prefs, const EOverlay overlay)
{
    // The row's own checkbox first (false out of range, so the table is only indexed for a valid row)
    if (!IsEnabled(prefs, overlay) || !IsMasterOn(prefs))
    {
        return false;
    }
    const FOverlayDef& def = OVERLAY_DEFS[static_cast<int32>(overlay)];
    return def.Parent == INDEX_NONE || IsEnabled(prefs, static_cast<EOverlay>(def.Parent));
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a row is one of the orbit-line rows
bool SOLOverlay::IsOrbitOverlay(const EOverlay overlay)
{
    return overlay == EOverlay::PlanetOrbits || overlay == EOverlay::AsteroidOrbits
        || overlay == EOverlay::MoonOrbits || overlay == EOverlay::OtherOrbits;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether an orbit category's lines show (the active profile's Alt+O switch, an orbit row, and IsShown)
bool SOLOverlay::IsOrbitShown(const FOverlayPrefs& prefs, const EOverlay overlay)
{
    return AreOrbitLinesOn(prefs) && IsOrbitOverlay(overlay) && IsShown(prefs, overlay);
}

//////////////////////////////////////////////////////////////////////////
// Returns the prefs made valid (profile bits limited, profile 0 enabled and used, the active profile enabled, masks
// limited, unused slots the defaults with their master and orbit bits on)
SOLOverlay::FOverlayPrefs SOLOverlay::Sanitized(const FOverlayPrefs& prefs)
{
    constexpr uint8 allProfiles = static_cast<uint8>(SOL::Overlay::ALL_PROFILES_MASK);
    constexpr uint8 firstProfile = 0x01;
    FOverlayPrefs result = prefs;
    result.EnabledProfiles = static_cast<uint8>((prefs.EnabledProfiles & allProfiles) | firstProfile);
    result.UsedProfiles = static_cast<uint8>(((prefs.UsedProfiles | result.EnabledProfiles) & allProfiles)
        | firstProfile);
    result.MasterOnProfiles = static_cast<uint8>(prefs.MasterOnProfiles & allProfiles);
    result.OrbitLinesProfiles = static_cast<uint8>(prefs.OrbitLinesProfiles & allProfiles);

    // The active profile: in range and enabled, else profile 0
    result.ActiveProfile = OverlayActiveSlot(prefs);
    if (!HasProfileBit(result.EnabledProfiles, result.ActiveProfile))
    {
        result.ActiveProfile = 0;
    }

    // A used slot (enabled now or before) keeps its content (D12); an unused one holds the defaults
    for (int32 profile = 0; profile < SOL::Overlay::MAX_PROFILES; ++profile)
    {
        if (HasProfileBit(result.UsedProfiles, profile))
        {
            result.Masks[profile] = prefs.Masks[profile] & AllBitsMask();
            continue;
        }
        result.Masks[profile] = DefaultMask();
        SetProfileBit(result.MasterOnProfiles, profile, true);
        SetProfileBit(result.OrbitLinesProfiles, profile, true);
    }
    return result;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether two prefs are equal after sanitizing (every profile bit set, the active profile and every mask)
bool SOLOverlay::AreEqual(const FOverlayPrefs& a, const FOverlayPrefs& b)
{
    const FOverlayPrefs x = Sanitized(a);
    const FOverlayPrefs y = Sanitized(b);
    if (x.EnabledProfiles != y.EnabledProfiles || x.UsedProfiles != y.UsedProfiles
        || x.MasterOnProfiles != y.MasterOnProfiles || x.OrbitLinesProfiles != y.OrbitLinesProfiles
        || x.ActiveProfile != y.ActiveProfile)
    {
        return false;
    }
    for (int32 profile = 0; profile < SOL::Overlay::MAX_PROFILES; ++profile)
    {
        if (x.Masks[profile] != y.Masks[profile])
        {
            return false;
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns the HUD profile tag's alpha: 1 while shown, then a linear fade to 0; 0 for a negative or non-finite time
float SOLOverlay::ProfileTagAlpha(const double elapsedS)
{
    if (!FMath::IsFinite(elapsedS) || elapsedS < 0.0)
    {
        return 0.0f;
    }
    if (elapsedS <= SOL::Overlay::PROFILE_TAG_SHOW_S)
    {
        return 1.0f;
    }
    const double fade = (elapsedS - SOL::Overlay::PROFILE_TAG_SHOW_S) / SOL::Overlay::PROFILE_TAG_FADE_S;
    return static_cast<float>(FMath::Clamp(1.0 - fade, 0.0, 1.0));
}

//////////////////////////////////////////////////////////////////////////
// Returns a profile mask as saved data: every row id mapped to whether it is checked
TMap<FString, bool> SOLOverlay::ToSavedProfile(const uint32 mask)
{
    TMap<FString, bool> saved;
    saved.Reserve(OVERLAY_COUNT);
    for (int32 overlay = 0; overlay < OVERLAY_COUNT; ++overlay)
    {
        saved.Add(OVERLAY_DEFS[overlay].Id, (mask & OverlayBit(overlay)) != 0);
    }
    return saved;
}

//////////////////////////////////////////////////////////////////////////
// Returns the mask of saved data (unknown ids ignored, missing rows at their default)
uint32 SOLOverlay::FromSavedProfile(const TMap<FString, bool>& saved)
{
    uint32 mask = DefaultMask();
    for (const TPair<FString, bool>& entry : saved)
    {
        const int32 overlay = FindById(entry.Key);
        if (overlay != INDEX_NONE)
        {
            mask = entry.Value ? mask | OverlayBit(overlay) : mask & ~OverlayBit(overlay);
        }
    }
    return mask;
}
