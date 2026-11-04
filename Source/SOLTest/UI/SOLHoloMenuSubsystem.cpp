/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHoloMenuSubsystem.h"

#include "Menu/SOLSettingsSubsystem.h"
#include "SOLTest.h"

#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns a cue's index into SOL::HoloMenu::CUES, or INDEX_NONE for Count and anything above it
    int32 HoloCueIndex(const ESOLHoloCue cue)
    {
        const int32 index = static_cast<int32>(cue);
        return index < static_cast<int32>(ESOLHoloCue::Count) ? index : INDEX_NONE;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a view size has two finite, positive components
    bool HoloIsValidViewSize(const FVector2f& viewSizePx)
    {
        return FMath::IsFinite(viewSizePx.X) && FMath::IsFinite(viewSizePx.Y) && viewSizePx.X > 0.0f
            && viewSizePx.Y > 0.0f;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the strength NaN-safe and clamped to the slider's range
float FSOLHoloMenuParamCache::SanitizeStrength(const float raw)
{
    return FMath::IsNaN(raw) ? 0.0f
        : FMath::Clamp(raw, SOL::Settings::HOLO_MENU_EFFECTS_MIN, SOL::Settings::HOLO_MENU_EFFECTS_MAX);
}

//////////////////////////////////////////////////////////////////////////
// Stores the sanitized strength and the (valid) view size; true when a push is due
bool FSOLHoloMenuParamCache::Update(const float rawStrength, const FVector2f& viewSizePx)
{
    const float strength = SanitizeStrength(rawStrength);
    const FVector2f viewSize = HoloIsValidViewSize(viewSizePx) ? viewSizePx : mViewSize;
    if (mHasPushed && strength == mStrength && viewSize == mViewSize)
    {
        return false;
    }
    mHasPushed = true;
    mStrength = strength;
    mViewSize = viewSize;
    ++mPushCount;
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Counts a request and accepts it on the cue's first finite time or its minimum interval after the last acceptance
bool FSOLHoloCueGate::Request(const ESOLHoloCue cue, const double nowS)
{
    const int32 index = HoloCueIndex(cue);
    if (index == INDEX_NONE)
    {
        return false;
    }
    ++mRequestCount[index];
    if (!FMath::IsFinite(nowS))
    {
        return false;
    }

    // A clock that went back gives a negative gap and is rejected like a too-early request
    if (mAcceptedCount[index] > 0 && !(nowS - mLastAcceptedS[index] >= SOL::HoloMenu::CUES[index].MinIntervalS))
    {
        return false;
    }
    mLastAcceptedS[index] = nowS;
    ++mAcceptedCount[index];
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns how many times a cue was requested (0 for an out-of-range cue)
int32 FSOLHoloCueGate::GetRequestCount(const ESOLHoloCue cue) const
{
    const int32 index = HoloCueIndex(cue);
    return index != INDEX_NONE ? mRequestCount[index] : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns how many times a cue was accepted (0 for an out-of-range cue)
int32 FSOLHoloCueGate::GetAcceptedCount(const ESOLHoloCue cue) const
{
    const int32 index = HoloCueIndex(cue);
    return index != INDEX_NONE ? mAcceptedCount[index] : 0;
}

//////////////////////////////////////////////////////////////////////////
// Creates the menu material instance, finds the settings and loads the cue sounds once (one warning per missing one)
void USOLHoloMenuSubsystem::OnWorldBeginPlay(UWorld& world)
{
    Super::OnWorldBeginPlay(world);
    mSettings = USOLSettingsSubsystem::Get(&world);

    // LOAD_NoWarn: the one warning below replaces the engine's own
    UMaterialInterface* material = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::HUD_SHAPE_MATERIAL, nullptr,
        LOAD_NoWarn);
    if (material != nullptr)
    {
        MenuMaterial = UMaterialInstanceDynamic::Create(material, this);
    }
    else
    {
        UE_LOG(LogSOL, Warning, TEXT("%s: %s is missing; the hologram menus draw without the shape material"),
            *GetName(), SOL::Paths::HUD_SHAPE_MATERIAL);
    }

    // The cue sounds: their assets carry the SFX sound class, so the SFX volume slider scales them
    CueSounds.SetNum(CUE_COUNT);
    int32 loadedCount = 0;
    for (int32 index = 0; index < CUE_COUNT; ++index)
    {
        const TCHAR* path = SOL::HoloMenu::CUES[index].Path;
        CueSounds[index] = LoadObject<USoundBase>(nullptr, path, nullptr, LOAD_NoWarn);
        if (CueSounds[index] == nullptr)
        {
            UE_LOG(LogSOL, Warning, TEXT("%s: %s is missing; that menu cue stays silent"), *GetName(), path);
            continue;
        }
        ++loadedCount;
    }
    UE_LOG(LogSOL, Log, TEXT("%s: menu material %s, %d of %d cue sounds loaded"), *GetName(),
        MenuMaterial != nullptr ? TEXT("created") : TEXT("missing"), loadedCount, CUE_COUNT);

    // An update made before begin play marked its values as pushed with no material: the new material gets them on
    // the next update
    mParamCache = FSOLHoloMenuParamCache();
    mLastUpdateFrame = MAX_uint64;
    mCompositeCvar = IConsoleManager::Get().FindConsoleVariable(SOL::Hologram::CVAR_HUD_COMPOSITE);
    mPushedComposite = -1.0f;
}

//////////////////////////////////////////////////////////////////////////
// Drops the material, the settings and the sounds
void USOLHoloMenuSubsystem::Deinitialize()
{
    MenuMaterial = nullptr;
    CueSounds.Reset();
    mSettings.Reset();
    Super::Deinitialize();
}

//////////////////////////////////////////////////////////////////////////
// Returns the sanitized live HoloMenuEffects, or the default with no settings subsystem
float USOLHoloMenuSubsystem::GetStrength() const
{
    const USOLSettingsSubsystem* settings = mSettings.Get();
    return settings != nullptr ? FSOLHoloMenuParamCache::SanitizeStrength(settings->GetLiveValues().HoloMenuEffects)
        : SOL::Settings::HOLO_MENU_EFFECTS_DEFAULT;
}

//////////////////////////////////////////////////////////////////////////
// Pushes the strength and the view size into the menu material only when the cache says one changed, and the
// composite switch only when the cvar changed
void USOLHoloMenuSubsystem::UpdateMaterial(const FVector2f& viewSizePx)
{
    const bool bIsPushDue = mParamCache.Update(GetStrength(), viewSizePx);
    if (MenuMaterial == nullptr)
    {
        return;
    }

    // sol.HudComposite (grain and scanlines) follows the HUD layer's material
    const float composite = mCompositeCvar != nullptr && mCompositeCvar->GetInt() != 0 ? 1.0f : 0.0f;
    if (composite != mPushedComposite)
    {
        mPushedComposite = composite;
        MenuMaterial->SetScalarParameterValue(SOL::HudShapeMaterialParams::COMPOSITE, composite);
    }
    if (!bIsPushDue)
    {
        return;
    }
    MenuMaterial->SetScalarParameterValue(SOL::HudShapeMaterialParams::EFFECTS_STRENGTH, mParamCache.GetStrength());

    // The size stays unset until a valid one arrived (a zero size would break the material's pixel mapping)
    const FVector2f viewSize = mParamCache.GetViewSize();
    if (HoloIsValidViewSize(viewSize))
    {
        MenuMaterial->SetScalarParameterValue(SOL::HudShapeMaterialParams::VIEW_WIDTH, viewSize.X);
        MenuMaterial->SetScalarParameterValue(SOL::HudShapeMaterialParams::VIEW_HEIGHT, viewSize.Y);
    }
}

//////////////////////////////////////////////////////////////////////////
// Feeds UpdateMaterial the game viewport's size once per frame (the first caller in a frame does the work)
void USOLHoloMenuSubsystem::UpdateMaterialForFrame()
{
    if (mLastUpdateFrame == GFrameCounter)
    {
        return;
    }
    mLastUpdateFrame = GFrameCounter;
    UpdateMaterial(GetViewSizePx());
}

//////////////////////////////////////////////////////////////////////////
// Returns the game viewport's size in pixels, or zero without a world or viewport
FVector2f USOLHoloMenuSubsystem::GetViewSizePx() const
{
    const UWorld* world = GetWorld();
    const UGameViewportClient* viewport = world != nullptr ? world->GetGameViewport() : nullptr;
    if (viewport == nullptr)
    {
        return FVector2f::ZeroVector;
    }
    FVector2D size = FVector2D::ZeroVector;
    viewport->GetViewportSize(size);
    return FVector2f(size);
}

//////////////////////////////////////////////////////////////////////////
// Plays a cue 2D (a UI sound, heard while paused) when the gate accepts it and its sound loaded, at any strength (D21)
bool USOLHoloMenuSubsystem::PlayCue(const ESOLHoloCue cue, const double nowS)
{
    if (!mCueGate.Request(cue, nowS))
    {
        return false;
    }
    const int32 index = HoloCueIndex(cue);
    USoundBase* sound = CueSounds.IsValidIndex(index) ? CueSounds[index].Get() : nullptr;
    UWorld* world = GetWorld();
    if (sound != nullptr && world != nullptr)
    {
        UGameplayStatics::PlaySound2D(world, sound, SOL::HoloMenu::CUES[index].Volume, 1.0f, 0.0f, nullptr, nullptr,
            /*bIsUISound=*/true);
        ++mPlayedCount[index];
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns how many times a cue's sound was started (0 for an out-of-range cue)
int32 USOLHoloMenuSubsystem::GetCuePlayedCount(const ESOLHoloCue cue) const
{
    const int32 index = HoloCueIndex(cue);
    return index != INDEX_NONE ? mPlayedCount[index] : 0;
}

//////////////////////////////////////////////////////////////////////////
// Game and PIE worlds only
bool USOLHoloMenuSubsystem::DoesSupportWorldType(const EWorldType::Type worldType) const
{
    return worldType == EWorldType::Game || worldType == EWorldType::PIE;
}
