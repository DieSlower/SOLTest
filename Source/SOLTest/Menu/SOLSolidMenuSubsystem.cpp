/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSolidMenuSubsystem.h"

#include "Menu/SOLSettingsSubsystem.h"
#include "SOLTest.h"

#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
    // The kit's texture paths, in ESOLSolidTexture order
    const TCHAR* const SOLID_TEXTURE_PATHS[] = { SOL::Paths::SOLID_TEX_RIVET, SOL::Paths::SOLID_TEX_VENT };
    static_assert(UE_ARRAY_COUNT(SOLID_TEXTURE_PATHS) == static_cast<int32>(ESOLSolidTexture::Count),
        "one path per ESOLSolidTexture");

    //////////////////////////////////////////////////////////////////////////
    // Loads one asset (LOAD_NoWarn: this warning replaces the engine's) and counts it when it is missing
    template <typename T>
    T* SolidMenuLoad(const UObject& owner, const TCHAR* path, const TCHAR* what, int32& missingCount)
    {
        T* asset = LoadObject<T>(nullptr, path, nullptr, LOAD_NoWarn);
        if (asset == nullptr)
        {
            ++missingCount;
            UE_LOG(LogSOL, Warning, TEXT("%s: %s is missing; the solid menus draw %s"), *owner.GetName(), path, what);
        }
        return asset;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the strength NaN-safe and clamped to the HUD effects range
float FSOLSolidMenuParamCache::SanitizeStrength(const float raw)
{
    return FMath::IsNaN(raw) ? 0.0f : FMath::Clamp(raw, SOL::Settings::HUD_EFFECTS_MIN, SOL::Settings::HUD_EFFECTS_MAX);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a DPI scale is finite and positive
bool FSOLSolidMenuParamCache::IsValidDpiScale(const float dpiScale)
{
    return FMath::IsFinite(dpiScale) && dpiScale > 0.0f;
}

//////////////////////////////////////////////////////////////////////////
// Stores the sanitized strength and the (valid) DPI scale; true when a push is due
bool FSOLSolidMenuParamCache::Update(const float rawStrength, const float dpiScale)
{
    const float strength = SanitizeStrength(rawStrength);
    const float dpi = IsValidDpiScale(dpiScale) ? dpiScale : mDpiScale;
    if (mHasPushed && strength == mStrength && dpi == mDpiScale)
    {
        return false;
    }
    mHasPushed = true;
    mStrength = strength;
    mDpiScale = dpi;
    ++mPushCount;
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns the subsystem of a world context object's world, or null
USOLSolidMenuSubsystem* USOLSolidMenuSubsystem::Get(const UObject* worldContext)
{
    if (worldContext == nullptr || GEngine == nullptr)
    {
        return nullptr;
    }
    const UWorld* world = GEngine->GetWorldFromContextObject(worldContext, EGetWorldErrorMode::ReturnNull);
    return world != nullptr ? world->GetSubsystem<USOLSolidMenuSubsystem>() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Pushes this frame's parameters and returns the time and strength a screen animates at this tick
FSOLSolidMenuFrame USOLSolidMenuSubsystem::BeginFrame(const UObject* worldContext)
{
    FSOLSolidMenuFrame frame;
    frame.NowS = FPlatformTime::Seconds();
    USOLSolidMenuSubsystem* subsystem = Get(worldContext);
    if (subsystem != nullptr)
    {
        subsystem->UpdateForFrame();
        frame.Strength = subsystem->GetStrength();
    }
    return frame;
}

//////////////////////////////////////////////////////////////////////////
// Loads the assets once and creates the shared style materials
void USOLSolidMenuSubsystem::Initialize(FSubsystemCollectionBase& collection)
{
    Super::Initialize(collection);
    LoadAssets();
}

//////////////////////////////////////////////////////////////////////////
// Finds the settings subsystem (the strength source)
void USOLSolidMenuSubsystem::OnWorldBeginPlay(UWorld& world)
{
    Super::OnWorldBeginPlay(world);
    mSettings = USOLSettingsSubsystem::Get(&world);
}

//////////////////////////////////////////////////////////////////////////
// Drops the unique material list and the settings (the UPROPERTY assets go with the subsystem; the panels' brushes
// reference their materials and textures themselves)
void USOLSolidMenuSubsystem::Deinitialize()
{
    mUniqueMaterials.Reset();
    mSettings.Reset();
    Super::Deinitialize();
}

//////////////////////////////////////////////////////////////////////////
// Reads the live strength and the viewport DPI scale once per frame and pushes them when either changed
void USOLSolidMenuSubsystem::UpdateForFrame()
{
    if (mLastUpdateFrame == GFrameCounter)
    {
        return;
    }
    mLastUpdateFrame = GFrameCounter;
    if (!mSettings.IsValid() && GetWorld() != nullptr)
    {
        mSettings = USOLSettingsSubsystem::Get(this);
    }
    const USOLSettingsSubsystem* settings = mSettings.Get();
    const float raw = settings != nullptr ? settings->GetLiveValues().HudEffectsStrength
        : SOL::Settings::HUD_EFFECTS_DEFAULT;
    PushParams(raw, GetViewportDpiScale());
}

//////////////////////////////////////////////////////////////////////////
// Pushes the strength and DPI scale into every shared and live unique material when the cache says one changed
void USOLSolidMenuSubsystem::PushParams(const float rawStrength, const float dpiScale)
{
    if (!mParamCache.Update(rawStrength, dpiScale))
    {
        return;
    }
    for (UMaterialInstanceDynamic* material : StyleMaterials)
    {
        if (material != nullptr)
        {
            WriteParams(*material);
        }
    }

    // Unique materials die with their panels: drop the dead ones while writing the live ones
    for (int32 index = mUniqueMaterials.Num() - 1; index >= 0; --index)
    {
        UMaterialInstanceDynamic* material = mUniqueMaterials[index].Get();
        if (material == nullptr)
        {
            mUniqueMaterials.RemoveAtSwap(index);
            continue;
        }
        WriteParams(*material);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the sanitized live HudEffectsStrength, or the default with no settings subsystem
float USOLSolidMenuSubsystem::GetStrength() const
{
    const USOLSettingsSubsystem* settings = mSettings.Get();
    return settings != nullptr ? FSOLSolidMenuParamCache::SanitizeStrength(settings->GetLiveValues().HudEffectsStrength)
        : SOL::Settings::HUD_EFFECTS_DEFAULT;
}

//////////////////////////////////////////////////////////////////////////
// Returns the game viewport's UMG DPI scale (1 without a world, a viewport or a valid size)
float USOLSolidMenuSubsystem::GetViewportDpiScale() const
{
    const UWorld* world = GetWorld();
    const UGameViewportClient* viewport = world != nullptr ? world->GetGameViewport() : nullptr;
    if (viewport == nullptr)
    {
        return 1.0f;
    }
    FVector2D size = FVector2D::ZeroVector;
    viewport->GetViewportSize(size);
    if (!(size.X > 0.0) || !(size.Y > 0.0))
    {
        return 1.0f;
    }
    const float scale = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(
        FIntPoint(FMath::RoundToInt(size.X), FMath::RoundToInt(size.Y)));
    return FSOLSolidMenuParamCache::IsValidDpiScale(scale) ? scale : 1.0f;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a style's material loaded
bool USOLSolidMenuSubsystem::HasStyleMaterial(const ESOLSolidStyle style) const
{
    return GetStyleMaterial(style) != nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Returns a style's shared material, or null when its asset is missing
UMaterialInstanceDynamic* USOLSolidMenuSubsystem::GetStyleMaterial(const ESOLSolidStyle style) const
{
    const int32 index = static_cast<int32>(style);
    return HullMaterial != nullptr && StyleMaterials.IsValidIndex(index) ? StyleMaterials[index].Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Creates a material of a style that only its owner draws with, current with the cached strength and DPI scale
UMaterialInstanceDynamic* USOLSolidMenuSubsystem::CreateUniqueMaterial(const ESOLSolidStyle style, UObject* owner)
{
    const UMaterialInstanceDynamic* shared = GetStyleMaterial(style);
    UMaterialInterface* parent = shared != nullptr ? shared->Parent.Get() : nullptr;
    if (parent == nullptr)
    {
        return nullptr;
    }
    UMaterialInstanceDynamic* material = UMaterialInstanceDynamic::Create(parent, owner != nullptr ? owner : this);
    WriteParams(*material);
    mUniqueMaterials.Add(material);
    return material;
}

//////////////////////////////////////////////////////////////////////////
// Returns a style texture, or null when it is missing
UTexture2D* USOLSolidMenuSubsystem::GetTexture(const ESOLSolidTexture texture) const
{
    const int32 index = static_cast<int32>(texture);
    return Textures.IsValidIndex(index) ? Textures[index].Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Returns how many unique materials are alive
int32 USOLSolidMenuSubsystem::GetUniqueMaterialCount() const
{
    int32 count = 0;
    for (const TWeakObjectPtr<UMaterialInstanceDynamic>& material : mUniqueMaterials)
    {
        count += material.IsValid() ? 1 : 0;
    }
    return count;
}

//////////////////////////////////////////////////////////////////////////
// Game and PIE worlds only
bool USOLSolidMenuSubsystem::DoesSupportWorldType(const EWorldType::Type worldType) const
{
    return worldType == EWorldType::Game || worldType == EWorldType::PIE;
}

//////////////////////////////////////////////////////////////////////////
// Loads every asset once (LOAD_NoWarn: one warning of ours per missing asset) and creates the shared materials
void USOLSolidMenuSubsystem::LoadAssets()
{
    if (mIsLoaded)
    {
        return;
    }
    mIsLoaded = true;
    mMissingAssetCount = 0;

    MenuFont = SolidMenuLoad<UFont>(*this, SOL::Paths::SOLID_MENU_FONT, TEXT("with the engine font"),
        mMissingAssetCount);
    HullMaterial = SolidMenuLoad<UMaterialInterface>(*this, SOL::Paths::SOLID_HULL_MATERIAL,
        TEXT("flat fallback hulls"), mMissingAssetCount);

    // One shared dynamic material per style instance, current with the cache's values
    StyleMaterials.SetNum(STYLE_COUNT);
    for (int32 index = 0; index < STYLE_COUNT; ++index)
    {
        UMaterialInterface* instance = SolidMenuLoad<UMaterialInterface>(*this,
            SOL::Paths::SOLID_STYLE_MATERIALS[index], TEXT("that style flat"), mMissingAssetCount);
        StyleMaterials[index] = instance != nullptr ? UMaterialInstanceDynamic::Create(instance, this) : nullptr;
        if (StyleMaterials[index] != nullptr)
        {
            WriteParams(*StyleMaterials[index]);
        }
    }
    Textures.SetNum(TEXTURE_COUNT);
    for (int32 index = 0; index < TEXTURE_COUNT; ++index)
    {
        Textures[index] = SolidMenuLoad<UTexture2D>(*this, SOLID_TEXTURE_PATHS[index], TEXT("without it"),
            mMissingAssetCount);
    }
    UE_LOG(LogSOL, Log, TEXT("%s: solid menu assets loaded, %d missing"), *GetName(), mMissingAssetCount);
}

//////////////////////////////////////////////////////////////////////////
// Writes the cached strength and DPI scale into one material
void USOLSolidMenuSubsystem::WriteParams(UMaterialInstanceDynamic& material) const
{
    material.SetScalarParameterValue(SOL::SolidMenuMaterialParams::EFFECTS_STRENGTH, mParamCache.GetStrength());
    material.SetScalarParameterValue(SOL::SolidMenuMaterialParams::DPI_SCALE, mParamCache.GetDpiScale());
}
