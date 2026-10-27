/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSettingsSubsystem.h"

#include "Menu/SOLSettingsRules.h"
#include "Menu/SOLSettingsSave.h"
#include "SOLConstants.h"
#include "SOLTest.h"

#include "AudioDevice.h"
#include "AudioThread.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"

namespace
{
    const TCHAR* const SETTINGS_SAVE_DIRECTORY = TEXT("SaveGames");
    const TCHAR* const SETTINGS_SAVE_EXTENSION = TEXT(".sav");
    constexpr float SETTINGS_UNIT_PITCH = 1.0f;

    //////////////////////////////////////////////////////////////////////////
    // Returns the engine's game user settings, or nullptr
    UGameUserSettings* SettingsGetEngineSettings()
    {
        return GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
    }
}

//////////////////////////////////////////////////////////////////////////
// Picks the save slot, loads it once, resolves the keys and loads the sound classes
void USOLSettingsSubsystem::Initialize(FSubsystemCollectionBase& collection)
{
    Super::Initialize(collection);

    // A menu smoke run writes its own slot, cleared first, so it starts from the defaults and never touches the
    // player's
    mSlotName = SOL::Settings::SAVE_SLOT;
    if (FCString::Strifind(FCommandLine::Get(), SOL::CommandLine::MENU_SMOKE_NAME) != nullptr)
    {
        mSlotName = SOL::Settings::SMOKE_SAVE_SLOT;
        UGameplayStatics::DeleteGameInSlot(mSlotName, SOL::Settings::SAVE_USER_INDEX);
    }
    SaveObject = NewObject<USOLSettingsSave>(this);
    SeedEngineGraphics();
    LoadOnce();

    // Sound classes are optional content: without them the volume settings are kept but have no effect
    MasterSoundClass = LoadObject<USoundClass>(nullptr, SOL::Paths::SOUND_CLASS_MASTER);
    SfxSoundClass = LoadObject<USoundClass>(nullptr, SOL::Paths::SOUND_CLASS_SFX);
    MusicSoundClass = LoadObject<USoundClass>(nullptr, SOL::Paths::SOUND_CLASS_MUSIC);
    if (MasterSoundClass == nullptr || SfxSoundClass == nullptr || MusicSoundClass == nullptr)
    {
        UE_LOG(LogSOL, Warning, TEXT("Settings %s: sound classes missing; volume settings have no effect"), *GetName());
    }
    VolumeMix = NewObject<USoundMix>(this, TEXT("SOLSettingsVolumeMix"));
}

//////////////////////////////////////////////////////////////////////////
// Writes synchronously if a write is still owed or in flight (shutdown), then lets go of everything
void USOLSettingsSubsystem::Deinitialize()
{
    // Unapplied changes are dropped; an applied but unwritten change, a failed write or an async write that has not
    // finished yet (it could be cut off by the shutdown, and its completion would never be seen) is written now,
    // synchronously. An unfinished async write carries the same content, so a late finish leaves the same file
    RevertPending();
    if ((IsSaveOwed() || mSaveWritesInFlight > 0) && SaveObject != nullptr)
    {
        FillSaveObject();
        const bool bSaved = UGameplayStatics::SaveGameToSlot(SaveObject, mSlotName, SOL::Settings::SAVE_USER_INDEX);
        UE_LOG(LogSOL, Log, TEXT("Settings %s: shutdown write of slot %s (%d async in flight) %s"), *GetName(),
            *mSlotName, mSaveWritesInFlight, bSaved ? TEXT("done") : TEXT("FAILED"));
        mIsSaveOwed = false;
        mSaveWritesInFlight = 0;
    }
    if (mIsEngineSettingsSaveOwed)
    {
        if (UGameUserSettings* engineSettings = SettingsGetEngineSettings())
        {
            engineSettings->SaveSettings();
        }
        mIsEngineSettingsSaveOwed = false;
    }
    mOnKeyBindingsChanged.Clear();
    mAudioWorld.Reset();
    Super::Deinitialize();
}

//////////////////////////////////////////////////////////////////////////
// Returns the settings subsystem of an object's game instance, or nullptr
USOLSettingsSubsystem* USOLSettingsSubsystem::Get(const UObject* worldContext)
{
    const UWorld* world = worldContext != nullptr ? worldContext->GetWorld() : nullptr;
    const UGameInstance* gameInstance = world != nullptr ? world->GetGameInstance() : nullptr;
    return gameInstance != nullptr ? gameInstance->GetSubsystem<USOLSettingsSubsystem>() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Reads the slot once: uses, migrates or ignores it by version, and seeds what it does not hold from the engine
void USOLSettingsSubsystem::LoadOnce()
{
    using SOLSettingsRules::ESaveVersionAction;
    SOLSettingsModel::FSettingsValues loaded;
    TMap<FString, FString> overrides;
    USaveGame* raw = UGameplayStatics::DoesSaveGameExist(mSlotName, SOL::Settings::SAVE_USER_INDEX)
        ? UGameplayStatics::LoadGameFromSlot(mSlotName, SOL::Settings::SAVE_USER_INDEX) : nullptr;
    USOLSettingsSave* save = Cast<USOLSettingsSave>(raw);
    const ESaveVersionAction action = save != nullptr
        ? SOLSettingsRules::ClassifySaveVersion(save->Version, SOL::Settings::SAVE_VERSION)
        : ESaveVersionAction::UseDefaults;
    if (action == ESaveVersionAction::Migrate)
    {
        // The upgraded save is written back once, at the next write point; a save from before the current default key
        // layout also gets the one-time notice
        mIsControlsChangedNoticePending = SOLKeyBindings::DefaultLayoutChangedSince(save->Version);
        MigrateSave(*save);
        mIsSaveOwed = true;
    }
    const bool bSaveUsed = action != ESaveVersionAction::UseDefaults;
    if (bSaveUsed)
    {
        loaded = save->GetValues();
        overrides = save->KeyOverrides;
        mIsVsyncChosen = save->bVsyncChosen;
        mIsQualityChosen = save->bQualityChosen;
        mWrittenShowOrbitLines = save->bShowOrbitLines;
    }
    else if (raw != nullptr)
    {
        UE_LOG(LogSOL, Warning, TEXT("Settings %s: slot %s ignored (unknown save or schema version %d)"),
            *GetName(), *mSlotName, save != nullptr ? save->Version : -1);
    }

    // Vsync and quality: only what the player chose is pushed (and only where it differs from the engine); the rest
    // starts from the engine, so a first run writes nothing and an engine custom scalability mix survives the boot
    bool bPushVsync = false;
    bool bPushQuality = false;
    if (const UGameUserSettings* engineSettings = SettingsGetEngineSettings())
    {
        const SOLSettingsRules::FBootGraphics boot = SOLSettingsRules::DecideBootGraphics(bSaveUsed, mIsVsyncChosen,
            mIsQualityChosen, loaded.bVsync, loaded.QualityPreset, engineSettings->IsVSyncEnabled(),
            engineSettings->GetOverallScalabilityLevel(), loaded.QualityPreset);
        loaded.bVsync = boot.bVsync;
        loaded.QualityPreset = boot.QualityPreset;
        bPushVsync = boot.bPushVsync;
        bPushQuality = boot.bPushQuality;
    }
    mModel = SOLSettingsModel::MakeModel(loaded);
    mAppliedKeys = SOLKeyBindings::ResolveKeys(overrides);
    mPendingKeys = mAppliedKeys;

    // The player's vsync and quality win over the engine's ini (applied now; its own write waits for the next write
    // point)
    PushEngineGraphics(bPushVsync, bPushQuality, false);
    UE_LOG(LogSOL, Log, TEXT("Settings %s: slot %s %s; %d rebound keys"), *GetName(), *mSlotName,
        action == ESaveVersionAction::UseDefaults ? TEXT("not used (defaults)") : TEXT("loaded"), overrides.Num());
}

//////////////////////////////////////////////////////////////////////////
// Returns true once when the loaded save predates the current default key layout
bool USOLSettingsSubsystem::ConsumeControlsChangedNotice()
{
    const bool bPending = mIsControlsChangedNoticePending;
    mIsControlsChangedNoticePending = false;
    return bPending;
}

//////////////////////////////////////////////////////////////////////////
// Upgrades an older save to the current schema in memory (USOLSettingsSave::MigrateToCurrentVersion)
void USOLSettingsSubsystem::MigrateSave(USOLSettingsSave& save)
{
    // The steps live on the save object (schema 1 -> 2: orbit lines, SDD 17); the upgraded save is written once
    save.MigrateToCurrentVersion();
}

//////////////////////////////////////////////////////////////////////////
// Reads the applied resolution and window mode from GameUserSettings
void USOLSettingsSubsystem::SeedEngineGraphics()
{
    if (const UGameUserSettings* engineSettings = SettingsGetEngineSettings())
    {
        mAppliedResolution = engineSettings->GetScreenResolution();
        mAppliedWindowMode = static_cast<int32>(engineSettings->GetFullscreenMode());
    }
    mPendingResolution = mAppliedResolution;
    mPendingWindowMode = mAppliedWindowMode;
}

//////////////////////////////////////////////////////////////////////////
// Pushes the chosen applied graphics values into GameUserSettings, applying only what differs from the engine
bool USOLSettingsSubsystem::PushEngineGraphics(const bool bVsync, const bool bQuality, const bool bResolution)
{
    UGameUserSettings* engineSettings = SettingsGetEngineSettings();
    if (engineSettings == nullptr)
    {
        return false;
    }
    bool bOtherChanged = false;
    if (bVsync && engineSettings->IsVSyncEnabled() != mModel.Saved.bVsync)
    {
        engineSettings->SetVSyncEnabled(mModel.Saved.bVsync);
        bOtherChanged = true;
    }
    if (bQuality && engineSettings->GetOverallScalabilityLevel() != mModel.Saved.QualityPreset)
    {
        engineSettings->SetOverallScalabilityLevel(mModel.Saved.QualityPreset);
        bOtherChanged = true;
    }
    bool bResolutionChanged = false;
    if (bResolution && mAppliedResolution.X > 0 && mAppliedResolution.Y > 0
        && engineSettings->GetScreenResolution() != mAppliedResolution)
    {
        engineSettings->SetScreenResolution(mAppliedResolution);
        bResolutionChanged = true;
    }
    if (bResolution && static_cast<int32>(engineSettings->GetFullscreenMode()) != mAppliedWindowMode)
    {
        engineSettings->SetFullscreenMode(static_cast<EWindowMode::Type>(mAppliedWindowMode));
        bResolutionChanged = true;
    }

    // The editor's PIE viewport is not the game window: there the resolution and window mode are only stored, for the
    // next standalone run
    if (bResolutionChanged)
    {
        if (GIsEditor)
        {
            UE_LOG(LogSOL, Log, TEXT("Settings %s: resolution and window mode stored, applied in a standalone run"),
                *GetName());
        }
        else
        {
            engineSettings->ApplyResolutionSettings(false);
        }
    }
    if (bOtherChanged)
    {
        engineSettings->ApplyNonResolutionSettings();
    }
    const bool bChanged = bOtherChanged || bResolutionChanged;
    mIsEngineSettingsSaveOwed |= bChanged;
    return bChanged;
}

//////////////////////////////////////////////////////////////////////////
// Sets the pending values (sanitized by the model) and previews them
void USOLSettingsSubsystem::SetPendingValues(const SOLSettingsModel::FSettingsValues& values)
{
    SOLSettingsModel::SetPending(mModel, values);
    ApplyAudio();
}

//////////////////////////////////////////////////////////////////////////
// Returns the resolutions offered (built on first use)
const TArray<FIntPoint>& USOLSettingsSubsystem::GetResolutionChoices()
{
    if (!mHasResolutionChoices)
    {
        TArray<FIntPoint> supported;
        UKismetSystemLibrary::GetSupportedFullscreenResolutions(supported);
        mResolutionChoices = SOLSettingsRules::BuildResolutionChoices(supported, mAppliedResolution);
        mHasResolutionChoices = true;
    }
    return mResolutionChoices;
}

//////////////////////////////////////////////////////////////////////////
// Sets the pending resolution (applied on Apply)
void USOLSettingsSubsystem::SetPendingResolution(const FIntPoint& resolution)
{
    if (resolution.X > 0 && resolution.Y > 0)
    {
        mPendingResolution = resolution;
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets the pending window mode (clamped; applied on Apply)
void USOLSettingsSubsystem::SetPendingWindowMode(const int32 mode)
{
    mPendingWindowMode = FMath::Clamp(mode, 0, SOLSettingsRules::WINDOW_MODE_COUNT - 1);
}

//////////////////////////////////////////////////////////////////////////
// Rebinds one pending key
SOLKeyBindings::ERebindResult USOLSettingsSubsystem::RebindKey(const int32 binding, const FString& keyName,
    int32& outConflict)
{
    const SOLKeyBindings::ERebindResult result = SOLKeyBindings::Rebind(mPendingKeys, binding, keyName, outConflict);
    UE_LOG(LogSOL, Log, TEXT("Settings %s: rebind %s -> %s: %s%s"), *GetName(), SOLKeyBindings::GetDef(binding).Id,
        *keyName, result == SOLKeyBindings::ERebindResult::Bound ? TEXT("bound")
        : (result == SOLKeyBindings::ERebindResult::Unchanged ? TEXT("unchanged") : TEXT("refused")),
        outConflict != INDEX_NONE ? TEXT(" (conflict)") : TEXT(""));
    return result;
}

//////////////////////////////////////////////////////////////////////////
// Sets every pending key back to its default
void USOLSettingsSubsystem::ResetKeysToDefaults()
{
    mPendingKeys = SOLKeyBindings::DefaultKeys();
}

//////////////////////////////////////////////////////////////////////////
// Returns whether anything pending differs from what is applied
bool USOLSettingsSubsystem::IsDirty() const
{
    return SOLSettingsModel::IsDirty(mModel) || mPendingKeys != mAppliedKeys
        || mPendingResolution != mAppliedResolution || mPendingWindowMode != mAppliedWindowMode;
}

//////////////////////////////////////////////////////////////////////////
// Applies everything pending; returns true if anything changed (the write happens at EndVisit)
bool USOLSettingsSubsystem::ApplyPending()
{
    // Values: the model decides whether the slot must be written
    const SOLSettingsModel::FSettingsValues before = mModel.Saved;
    const bool bValuesChanged = SOLSettingsModel::CommitIfDirty(mModel);
    mIsSaveOwed |= bValuesChanged;

    // A vsync or quality the player changed is theirs from now on: it is pushed at every boot (DecideBootGraphics)
    const bool bVsyncChanged = before.bVsync != mModel.Saved.bVsync;
    const bool bQualityChanged = before.QualityPreset != mModel.Saved.QualityPreset;
    mIsVsyncChosen |= bVsyncChanged;
    mIsQualityChosen |= bQualityChanged;

    // Keys: the same slot; the pawn rebuilds its mapping from the broadcast
    const bool bKeysChanged = mPendingKeys != mAppliedKeys;
    if (bKeysChanged)
    {
        mAppliedKeys = mPendingKeys;
        mIsSaveOwed = true;
    }

    // Engine graphics: only the values the player changed are pushed, and only where they differ from the engine (so
    // a custom scalability mix survives an Apply that did not touch the preset); GameUserSettings is written at the
    // next write point
    const bool bResolutionChanged = mPendingResolution != mAppliedResolution
        || mPendingWindowMode != mAppliedWindowMode;
    mAppliedResolution = mPendingResolution;
    mAppliedWindowMode = mPendingWindowMode;
    const bool bEngineChanged = PushEngineGraphics(bVsyncChanged, bQualityChanged, bResolutionChanged);
    UE_LOG(LogSOL, Log, TEXT("Settings %s: apply (values %d, keys %d, engine graphics %d)"), *GetName(),
        bValuesChanged, bKeysChanged, bEngineChanged);
    if (bKeysChanged)
    {
        mOnKeyBindingsChanged.Broadcast();
    }
    return bValuesChanged || bKeysChanged || bResolutionChanged || bEngineChanged;
}

//////////////////////////////////////////////////////////////////////////
// Discards everything pending and previews the applied values again
void USOLSettingsSubsystem::RevertPending()
{
    const bool bWasDirty = IsDirty();
    SOLSettingsModel::Revert(mModel);
    mPendingKeys = mAppliedKeys;
    mPendingResolution = mAppliedResolution;
    mPendingWindowMode = mAppliedWindowMode;
    if (bWasDirty)
    {
        ApplyAudio();
    }
}

//////////////////////////////////////////////////////////////////////////
// Ends a settings visit: reverts what was not applied, then writes once if owed
void USOLSettingsSubsystem::EndVisit()
{
    RevertPending();
    FlushSave();
}

//////////////////////////////////////////////////////////////////////////
// Starts the owed writes: one async save of the slot and/or one GameUserSettings save
void USOLSettingsSubsystem::FlushSave()
{
    if (IsSaveOwed() && SaveObject != nullptr)
    {
        // The save object is serialized to memory here, on the game thread; only the file write runs async. The write
        // counts as in flight until it reports back (a failure makes the write owed again, possibly at once)
        FillSaveObject();
        ++mSaveRequestCount;
        ++mSaveWritesInFlight;
        mIsSaveOwed = false;
        UGameplayStatics::AsyncSaveGameToSlot(SaveObject, mSlotName, SOL::Settings::SAVE_USER_INDEX,
            FAsyncSaveGameToSlotDelegate::CreateUObject(this, &USOLSettingsSubsystem::HandleSaveCompleted));
        UE_LOG(LogSOL, Log, TEXT("Settings %s: writing slot %s (write %d)"), *GetName(), *mSlotName,
            mSaveRequestCount);
    }
    if (mIsEngineSettingsSaveOwed)
    {
        if (UGameUserSettings* engineSettings = SettingsGetEngineSettings())
        {
            engineSettings->SaveSettings();
            ++mEngineSettingsWriteCount;
        }
        mIsEngineSettingsSaveOwed = false;
    }
}

//////////////////////////////////////////////////////////////////////////
// Async save finished
void USOLSettingsSubsystem::HandleSaveCompleted(const FString& slotName, const int32 /*userIndex*/, const bool bSuccess)
{
    mSaveWritesInFlight = FMath::Max(0, mSaveWritesInFlight - 1);
    if (bSuccess)
    {
        ++mSaveCompletedCount;
        return;
    }

    // The player's data is still only in memory: the write is owed again (next write point, or shutdown)
    mIsSaveOwed = true;
    UE_LOG(LogSOL, Error, TEXT("Settings %s: writing slot %s failed; retried at the next write point"), *GetName(),
        *slotName);
}

//////////////////////////////////////////////////////////////////////////
// Copies the applied values, keys and chosen flags into the save object
void USOLSettingsSubsystem::FillSaveObject()
{
    SaveObject->Version = SOL::Settings::SAVE_VERSION;
    SaveObject->SetValues(mModel.Saved);
    SaveObject->KeyOverrides = SOLKeyBindings::MakeOverrides(mAppliedKeys);
    SaveObject->bVsyncChosen = mIsVsyncChosen;
    SaveObject->bQualityChosen = mIsQualityChosen;
    mWrittenShowOrbitLines = mModel.Saved.bShowOrbitLines;
}

//////////////////////////////////////////////////////////////////////////
// Sets the applied (and pending) orbit-line flag from the O key; the write waits for the next write point
void USOLSettingsSubsystem::SetOrbitLinesShown(const bool bShown)
{
    SOLSettingsModel::SetAppliedShowOrbitLines(mModel, bShown);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the slot needs a write: an Apply or a migration owes one, or the O key left the orbit-line flag
// different from what is on disk
bool USOLSettingsSubsystem::IsSaveOwed() const
{
    return SOLSettingsRules::IsSaveOwed(mIsSaveOwed, mModel.Saved.bShowOrbitLines, mWrittenShowOrbitLines);
}

//////////////////////////////////////////////////////////////////////////
// Pops the volume mix from a world that is being torn down (pushed by ApplyToWorld)
void USOLSettingsSubsystem::ReleaseWorld(UWorld& world)
{
    if (mAudioWorld.Get() != &world)
    {
        return;
    }
    if (VolumeMix != nullptr && MasterSoundClass != nullptr)
    {
        UGameplayStatics::PopSoundMixModifier(&world, VolumeMix);
    }
    mAudioWorld.Reset();
}

//////////////////////////////////////////////////////////////////////////
// Pushes the volume mix on a newly begun world and applies the volumes there
void USOLSettingsSubsystem::ApplyToWorld(UWorld& world)
{
    mAudioWorld = &world;
    if (VolumeMix != nullptr && MasterSoundClass != nullptr)
    {
        UGameplayStatics::PushSoundMixModifier(&world, VolumeMix);
    }
    ApplyAudio();
}

//////////////////////////////////////////////////////////////////////////
// Sets the volume overrides from the live values on the audio world
void USOLSettingsSubsystem::ApplyAudio()
{
    UWorld* world = mAudioWorld.Get();
    if (world == nullptr || VolumeMix == nullptr || MasterSoundClass == nullptr || SfxSoundClass == nullptr
        || MusicSoundClass == nullptr)
    {
        return;
    }

    // Master scales its children (effects and music) too; the adjusters multiply
    const SOLSettingsModel::FSettingsValues& values = mModel.Pending;
    UGameplayStatics::SetSoundMixClassOverride(world, VolumeMix, MasterSoundClass, values.MasterVolume,
        SETTINGS_UNIT_PITCH, SOL::Settings::AUDIO_FADE_S, true);
    UGameplayStatics::SetSoundMixClassOverride(world, VolumeMix, SfxSoundClass, values.SfxVolume, SETTINGS_UNIT_PITCH,
        SOL::Settings::AUDIO_FADE_S, false);
    UGameplayStatics::SetSoundMixClassOverride(world, VolumeMix, MusicSoundClass, values.MusicVolume,
        SETTINGS_UNIT_PITCH, SOL::Settings::AUDIO_FADE_S, false);
}

//////////////////////////////////////////////////////////////////////////
// Returns the absolute path of the save slot's file
FString USOLSettingsSubsystem::GetSlotFilePath() const
{
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / SETTINGS_SAVE_DIRECTORY
        / (mSlotName + SETTINGS_SAVE_EXTENSION));
}

//////////////////////////////////////////////////////////////////////////
// Reads the slot from disk again, as a restart would
bool USOLSettingsSubsystem::ReadSlotForVerification(SOLSettingsModel::FSettingsValues& outValues,
    TArray<FString>& outKeys) const
{
    const USOLSettingsSave* save = Cast<USOLSettingsSave>(UGameplayStatics::LoadGameFromSlot(mSlotName,
        SOL::Settings::SAVE_USER_INDEX));
    if (save == nullptr || SOLSettingsRules::ClassifySaveVersion(save->Version, SOL::Settings::SAVE_VERSION)
        == SOLSettingsRules::ESaveVersionAction::UseDefaults)
    {
        return false;
    }
    outValues = SOLSettingsModel::Sanitized(save->GetValues());
    outKeys = SOLKeyBindings::ResolveKeys(save->KeyOverrides);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Asks the audio thread for the effects class's current mixed volume
void USOLSettingsSubsystem::ProbeEffectsVolume()
{
    *mProbedEffectsVolume = -1.0f;
    UWorld* world = mAudioWorld.Get();
    FAudioDeviceHandle device = world != nullptr ? world->GetAudioDevice() : FAudioDeviceHandle();
    if (!device.IsValid() || SfxSoundClass == nullptr)
    {
        return;
    }
    FAudioDevice* audioDevice = device.GetAudioDevice();
    USoundClass* soundClass = SfxSoundClass;
    TSharedRef<float, ESPMode::ThreadSafe> result = mProbedEffectsVolume;
    FAudioThread::RunCommandOnAudioThread([audioDevice, soundClass, result]()
    {
        const FSoundClassProperties* properties = audioDevice->GetSoundClassCurrentProperties(soundClass);
        *result = properties != nullptr ? properties->Volume : -1.0f;
    });
}
