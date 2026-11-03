/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLOverlayModel.h"

namespace
{
    // Parent row of the eight icon categories
    constexpr int32 OVERLAY_ICONS_ROW = static_cast<int32>(SOLOverlay::EOverlay::Icons);

    // Every overlay row in EOverlay order (Overlay Menu order): id (persisted), label, default, parent row and whether
    // the master O key leaves it alone (only the Space Dust world effect, SDD 43)
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
        { TEXT("Darkening"), TEXT("Darkening"), true, INDEX_NONE },
        { TEXT("SpaceDust"), TEXT("Space Dust"), true, INDEX_NONE, true },
    };
    static_assert(UE_ARRAY_COUNT(OVERLAY_DEFS) == SOLOverlay::OVERLAY_COUNT, "one definition per overlay");
    static_assert(SOLOverlay::OVERLAY_COUNT <= 32, "a profile mask holds 32 overlays");

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
// Returns one profile holding the defaults (every slot holds the defaults)
SOLOverlay::FOverlayPrefs SOLOverlay::MakeDefaultPrefs()
{
    FOverlayPrefs prefs;
    for (uint32& mask : prefs.Masks)
    {
        mask = DefaultMask();
    }
    prefs.ProfileCount = 1;
    prefs.ActiveProfile = 0;
    return prefs;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a row is checked in the active profile
bool SOLOverlay::IsEnabled(const FOverlayPrefs& prefs, const EOverlay overlay)
{
    const int32 index = static_cast<int32>(overlay);
    const int32 profile = FMath::Clamp(prefs.ActiveProfile, 0, SOL::Overlay::MAX_PROFILES - 1);
    return OverlayIsValid(index) && (prefs.Masks[profile] & OverlayBit(index)) != 0;
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
    uint32& mask = prefs.Masks[FMath::Clamp(prefs.ActiveProfile, 0, SOL::Overlay::MAX_PROFILES - 1)];
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
// Returns whether an overlay shows now: its row checked, the master on (unless the row ignores it) and its parent row
// checked
bool SOLOverlay::IsShown(const FOverlayPrefs& prefs, const bool bMasterOn, const EOverlay overlay)
{
    // The row's own checkbox first (false out of range, so the table is only indexed for a valid row)
    if (!IsEnabled(prefs, overlay))
    {
        return false;
    }
    const FOverlayDef& def = OVERLAY_DEFS[static_cast<int32>(overlay)];
    if (!bMasterOn && !def.bIgnoresMaster)
    {
        return false;
    }
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
// Returns whether an orbit category's lines show (IsShown and the Alt+O orbit lines switch)
bool SOLOverlay::IsOrbitShown(const FOverlayPrefs& prefs, const bool bMasterOn, const bool bOrbitLinesOn,
    const EOverlay overlay)
{
    return bOrbitLinesOn && IsOrbitOverlay(overlay) && IsShown(prefs, bMasterOn, overlay);
}

//////////////////////////////////////////////////////////////////////////
// Returns the prefs made valid (counts and the active profile clamped, masks limited, unused slots the defaults)
SOLOverlay::FOverlayPrefs SOLOverlay::Sanitized(const FOverlayPrefs& prefs)
{
    FOverlayPrefs result = prefs;
    result.ProfileCount = FMath::Clamp(prefs.ProfileCount, 1, SOL::Overlay::MAX_PROFILES);
    result.ActiveProfile = FMath::Clamp(prefs.ActiveProfile, 0, result.ProfileCount - 1);
    for (int32 profile = 0; profile < SOL::Overlay::MAX_PROFILES; ++profile)
    {
        result.Masks[profile] = profile < result.ProfileCount ? prefs.Masks[profile] & AllBitsMask() : DefaultMask();
    }
    return result;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether two prefs are equal after sanitizing
bool SOLOverlay::AreEqual(const FOverlayPrefs& a, const FOverlayPrefs& b)
{
    const FOverlayPrefs x = Sanitized(a);
    const FOverlayPrefs y = Sanitized(b);
    if (x.ProfileCount != y.ProfileCount || x.ActiveProfile != y.ActiveProfile)
    {
        return false;
    }
    for (int32 profile = 0; profile < x.ProfileCount; ++profile)
    {
        if (x.Masks[profile] != y.Masks[profile])
        {
            return false;
        }
    }
    return true;
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
