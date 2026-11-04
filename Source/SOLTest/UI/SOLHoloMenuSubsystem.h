/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "SOLConstants.h"

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "SOLHoloMenuSubsystem.generated.h"

class IConsoleVariable;
class UMaterialInstanceDynamic;
class USOLSettingsSubsystem;
class USoundBase;

// The hologram menu sound cues (SDD 42 sections 4.6 and 7.1); each value indexes SOL::HoloMenu::CUES
enum class ESOLHoloCue : uint8
{
    Open = SOL::HoloMenu::CUE_OPEN,
    Hover = SOL::HoloMenu::CUE_HOVER,
    Select = SOL::HoloMenu::CUE_SELECT,
    Count,
};

static_assert(static_cast<int32>(ESOLHoloCue::Count) == static_cast<int32>(UE_ARRAY_COUNT(SOL::HoloMenu::CUES)),
    "ESOLHoloCue needs one entry per SOL::HoloMenu::CUES row");

/**
 * The menu material's parameter cache (SDD 42 section 4.6): says when the sanitized effects strength or the view size
 * changed since the last push, so the material parameters are set only on a change. A view size with a non-finite or
 * non-positive component keeps the last pushed size.
 */
struct SOLTEST_API FSOLHoloMenuParamCache
{
    // Returns the strength NaN-safe and clamped to [HOLO_MENU_EFFECTS_MIN, HOLO_MENU_EFFECTS_MAX] (NaN reads 0)
    static float SanitizeStrength(float raw);

    // Stores the values and returns true when a push is due: the first call, or an exact change of either value
    bool Update(float rawStrength, const FVector2f& viewSizePx);

    // Returns the last sanitized strength (HOLO_MENU_EFFECTS_DEFAULT before the first call)
    float GetStrength() const { return mStrength; }

    // Returns the last pushed view size (zero until a valid size was given)
    FVector2f GetViewSize() const { return mViewSize; }

    // Returns how many pushes Update asked for
    int32 GetPushCount() const { return mPushCount; }

private:

    float mStrength = SOL::Settings::HOLO_MENU_EFFECTS_DEFAULT;
    FVector2f mViewSize = FVector2f::ZeroVector;
    int32 mPushCount = 0;
    bool mHasPushed = false;
};

/**
 * The cue rate limiter (SDD 42 section 4.6): a cue is accepted on its first finite request and then at most once per
 * SOL::HoloMenu::CUES[cue].MinIntervalS; NaN, infinite and backwards times are rejected. Cues are independent of each
 * other and of the hologram strength (D21).
 */
struct SOLTEST_API FSOLHoloCueGate
{
    // Counts the request and returns whether the cue may play now (false and uncounted for an out-of-range cue)
    bool Request(ESOLHoloCue cue, double nowS);

    // Returns how many times a cue was requested
    int32 GetRequestCount(ESOLHoloCue cue) const;

    // Returns how many times a cue was accepted
    int32 GetAcceptedCount(ESOLHoloCue cue) const;

private:

    static constexpr int32 CUE_COUNT = static_cast<int32>(ESOLHoloCue::Count);

    double mLastAcceptedS[CUE_COUNT] = {};
    int32 mRequestCount[CUE_COUNT] = {};
    int32 mAcceptedCount[CUE_COUNT] = {};
};

/**
 * The hologram menus' shared world state (SDD 42 sections 4.3 and 4.6): owns the menu instance of M_SOLHudShape whose
 * effects strength is the Settings "Hologram menu effects" value (HoloMenuEffects), pushed with the view size only when
 * they change, and plays the open / hover / select cues 2D through their assets' SFX sound class. The cues are rate
 * limited per cue and play at any strength, 0 included (D21). Nothing ticks; the menus call UpdateMaterial and PlayCue.
 * A bare, never-initialized subsystem (tests) has no material, settings or sounds and never crashes.
 */
UCLASS()
class SOLTEST_API USOLHoloMenuSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    // Creates the menu material instance, finds the settings and loads the cue sounds once
    virtual void OnWorldBeginPlay(UWorld& world) override;

    // Drops the material, settings and sounds
    virtual void Deinitialize() override;

    // Returns the sanitized live HoloMenuEffects (HOLO_MENU_EFFECTS_DEFAULT with no settings subsystem)
    float GetStrength() const;

    // Pushes the strength and the view size into the menu material when either changed, and the sol.HudComposite
    // grain / scanline switch when it changed (as the HUD layer's material gets it)
    void UpdateMaterial(const FVector2f& viewSizePx);

    // The one per-frame entry every user calls (the HUD's menu layer, each frame widget): the first call in a frame
    // feeds UpdateMaterial the game viewport's size (the one size source, the HUD canvas size); later calls in the same
    // frame do nothing
    void UpdateMaterialForFrame();

    // Returns the game viewport's size in pixels (zero without one), the size UpdateMaterialForFrame pushes
    FVector2f GetViewSizePx() const;

    // Returns the view size the material was last given (zero until a valid one was pushed): what the frames read
    // after UpdateMaterialForFrame instead of asking the viewport again
    FVector2f GetMaterialViewSizePx() const { return mParamCache.GetViewSize(); }

    // Plays a cue 2D when the cue gate accepts it and its sound loaded; returns the gate's answer
    bool PlayCue(ESOLHoloCue cue, double nowS);

    // Returns the menu material instance the menu layer and the frames draw with, or null
    UMaterialInstanceDynamic* GetMenuMaterial() const { return MenuMaterial; }

    // Returns how many parameter pushes were due (with or without a material)
    int32 GetParamPushCount() const { return mParamCache.GetPushCount(); }

    // Returns how many times a cue was requested
    int32 GetCueRequestCount(ESOLHoloCue cue) const { return mCueGate.GetRequestCount(cue); }

    // Returns how many times a cue was accepted by the rate limit
    int32 GetCueAcceptedCount(ESOLHoloCue cue) const { return mCueGate.GetAcceptedCount(cue); }

    // Returns how many times a cue's sound was started (accepted and loaded, in a world)
    int32 GetCuePlayedCount(ESOLHoloCue cue) const;

protected:

    // Game and PIE worlds only
    virtual bool DoesSupportWorldType(const EWorldType::Type worldType) const override;

private:

    static constexpr int32 CUE_COUNT = static_cast<int32>(ESOLHoloCue::Count);

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> MenuMaterial;

    UPROPERTY(Transient)
    TArray<TObjectPtr<USoundBase>> CueSounds;           // One per cue (null when its asset is missing), set once

    TWeakObjectPtr<USOLSettingsSubsystem> mSettings;    // Where HoloMenuEffects is read from
    FSOLHoloMenuParamCache mParamCache;
    FSOLHoloCueGate mCueGate;
    int32 mPlayedCount[CUE_COUNT] = {};
    uint64 mLastUpdateFrame = MAX_uint64;               // GFrameCounter of the last UpdateMaterialForFrame (none)
    IConsoleVariable* mCompositeCvar = nullptr;         // sol.HudComposite, found once at begin play
    float mPushedComposite = -1.0f;                     // The Composite value the material has (-1: none pushed)
};
