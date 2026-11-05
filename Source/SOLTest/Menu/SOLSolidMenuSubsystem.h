/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLSolidStyle.h"
#include "SOLConstants.h"

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "SOLSolidMenuSubsystem.generated.h"

class UFont;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USOLSettingsSubsystem;
class UTexture2D;

// The solid menu textures the kit draws itself (SDD 46 section 10), in the order USOLSolidMenuSubsystem loads them;
// the grain (T_SOLSolidBrushed) is sampled by M_SOLSolidHull, which references it (and the hazard as its cue mask)
// itself
enum class ESOLSolidTexture : uint8
{
    Rivet,      // USOLSolidPanel's corner rivets
    Vent,       // USOLSolidPanel's top-right vent strip
    Hazard,     // The main menu's hazard stripe under Quit (46d): the stripe mask, tinted from the palette
    Count,
};

/**
 * The hull materials' parameter cache (SDD 46 section 4.3): says when the sanitized effects strength or the DPI scale
 * changed since the last push, so the shared materials are written only on a change (never per frame). Pure, headless.
 */
struct SOLTEST_API FSOLSolidMenuParamCache
{
    // Returns the strength NaN-safe and clamped to [HUD_EFFECTS_MIN, HUD_EFFECTS_MAX] (NaN reads 0), the rule of
    // FSOLHoloMenuParamCache::SanitizeStrength on the HUD effects range (Q1)
    static float SanitizeStrength(float raw);

    // Returns whether a DPI scale is usable: finite and positive
    static bool IsValidDpiScale(float dpiScale);

    // Stores the values and returns true when a push is due: the first call, or an exact change of the sanitized
    // strength or of a valid DPI scale; an invalid DPI scale keeps the last valid one (1 before any)
    bool Update(float rawStrength, float dpiScale);

    // Returns the last sanitized strength (HUD_EFFECTS_DEFAULT before the first call)
    float GetStrength() const { return mStrength; }

    // Returns the last valid DPI scale (1 before any)
    float GetDpiScale() const { return mDpiScale; }

    // Returns how many pushes Update asked for
    int32 GetPushCount() const { return mPushCount; }

private:

    float mStrength = SOL::Settings::HUD_EFFECTS_DEFAULT;
    float mDpiScale = 1.0f;
    int32 mPushCount = 0;
    bool mHasPushed = false;
};

// The time and effects strength a solid screen animates its widgets at during one tick (USOLSolidMenuSubsystem::
// BeginFrame), read once per screen tick and handed to every button (USOLMenuButton::RefreshHighlightAt)
struct FSOLSolidMenuFrame
{
    double NowS = 0.0;                                      // Real application seconds (FPlatformTime::Seconds)
    float Strength = SOL::Settings::HUD_EFFECTS_DEFAULT;    // Sanitized HudEffectsStrength
};

/**
 * The solid menus' shared world state (SDD 46 sections 4.1-4.3): loads the menu font F_SOLMenu, the hull material
 * M_SOLSolidHull, its style instances (SOL::Paths::SOLID_STYLE_MATERIALS) and the style textures once at initialize,
 * with one warning per missing asset (the kit then draws flat fallback brushes and the engine font); owns one shared
 * dynamic material per style (UPROPERTY TObjectPtr), which every button, row and control of that style draws with,
 * their state travelling in the brush tint (SOLSolidStyle::BrushTint); and pushes the effects strength (the Settings
 * HudEffectsStrength, Q1) and the viewport's DPI scale into every material it made, only when one changed. The panels
 * with a unique shape (main menu hull, page shell, pause side panel) get their own instance from CreateUniqueMaterial,
 * owned by the panel and kept current by the same pushes. Nothing ticks: each solid screen calls BeginFrame (which
 * runs UpdateForFrame) once from its tick; the first call in a frame does the work. A bare, never-initialized
 * subsystem (tests) has no assets and never crashes.
 */
UCLASS()
class SOLTEST_API USOLSolidMenuSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    // Returns the subsystem of a world context object's world, or null
    static USOLSolidMenuSubsystem* Get(const UObject* worldContext);

    // Runs the context's subsystem's UpdateForFrame and returns the frame a solid screen animates at this tick: the
    // real application time and the live strength (HUD_EFFECTS_DEFAULT without a subsystem); one call per screen tick
    static FSOLSolidMenuFrame BeginFrame(const UObject* worldContext);

    // Loads the assets once and creates the shared style materials
    virtual void Initialize(FSubsystemCollectionBase& collection) override;

    // Finds the settings subsystem (the strength source)
    virtual void OnWorldBeginPlay(UWorld& world) override;

    // Drops the unique material list and the settings subsystem
    virtual void Deinitialize() override;

    // Reads the live strength and the viewport DPI scale once per frame (later calls in the same frame do nothing) and
    // pushes them into every material when either changed
    void UpdateForFrame();

    // Pushes a strength and a DPI scale into every material when the cache says one changed (UpdateForFrame's work,
    // reachable without a viewport)
    void PushParams(float rawStrength, float dpiScale);

    // Returns the sanitized live HudEffectsStrength (HUD_EFFECTS_DEFAULT with no settings subsystem)
    float GetStrength() const;

    // Returns the last pushed DPI scale (1 before any)
    float GetDpiScale() const { return mParamCache.GetDpiScale(); }

    // Returns the game viewport's UMG DPI scale (1 without a viewport), the scale UpdateForFrame pushes
    float GetViewportDpiScale() const;

    // Returns whether the hull material and the instance of a style loaded (false: that style draws flat)
    bool HasStyleMaterial(ESOLSolidStyle style) const;

    // Returns the shared material of a style, or null when its asset is missing
    UMaterialInstanceDynamic* GetStyleMaterial(ESOLSolidStyle style) const;

    // Creates a material of a style that only its owner draws with (a panel with its own step, chin or notches),
    // outered to the owner, with the current strength and DPI scale; later pushes reach it while it lives (weakly
    // held). Null when the style's asset is missing
    UMaterialInstanceDynamic* CreateUniqueMaterial(ESOLSolidStyle style, UObject* owner);

    // Returns the menu font F_SOLMenu, or null when it is missing (the kit then uses the engine default font)
    UFont* GetMenuFont() const { return MenuFont; }

    // Returns a style texture, or null when it is missing
    UTexture2D* GetTexture(ESOLSolidTexture texture) const;

    // Returns how many assets were missing at load (each logged one warning)
    int32 GetMissingAssetCount() const { return mMissingAssetCount; }

    // Returns how many parameter pushes were due
    int32 GetParamPushCount() const { return mParamCache.GetPushCount(); }

    // Returns how many unique materials are alive and kept current
    int32 GetUniqueMaterialCount() const;

protected:

    // Game and PIE worlds only
    virtual bool DoesSupportWorldType(const EWorldType::Type worldType) const override;

private:

    static constexpr int32 STYLE_COUNT = static_cast<int32>(ESOLSolidStyle::Count);
    static constexpr int32 TEXTURE_COUNT = static_cast<int32>(ESOLSolidTexture::Count);

    // Loads every asset once (one warning per missing one) and creates the shared materials
    void LoadAssets();

    // Writes the cached strength and DPI scale into one material
    void WriteParams(UMaterialInstanceDynamic& material) const;

    UPROPERTY(Transient)
    TObjectPtr<UFont> MenuFont;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> HullMaterial;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInstanceDynamic>> StyleMaterials;    // One per ESOLSolidStyle (null when missing)

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTexture2D>> Textures;                        // One per ESOLSolidTexture (null when missing)

    TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> mUniqueMaterials;  // Owned by their panels; compacted on push
    TWeakObjectPtr<USOLSettingsSubsystem> mSettings;                    // Where HudEffectsStrength is read from
    FSOLSolidMenuParamCache mParamCache;
    uint64 mLastUpdateFrame = MAX_uint64;                               // GFrameCounter of the last UpdateForFrame
    int32 mMissingAssetCount = 0;
    bool mIsLoaded = false;
};
