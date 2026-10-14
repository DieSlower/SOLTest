/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLSettingsModel.h"

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "SOLSettingsSubsystem.generated.h"

class USOLSettingsSave;
class USoundClass;
class USoundMix;
class UWorld;

// Fired after the applied key bindings change (Apply with rebinds); the ship pawn rebuilds its mapping context
DECLARE_MULTICAST_DELEGATE(FSOLOnKeyBindingsChanged);

/**
 * Player settings (SDD 8 decisions 5, 6 and 8, part 8d). Owns an SOLSettingsModel::FSettingsModel (saved values and
 * the pending values the Settings page edits), the rebindable key array (SOLKeyBindings), the pending resolution and
 * window mode, and applies them:
 *
 * - Live (pending) values preview at once: audio volumes (a runtime USoundMix overriding the SC_SOLMaster, SC_SOLSfx
 *   and SC_SOLMusic sound classes), mouse sensitivity and invert Y (read by ASOLShipPawn per look event), HUD scale and
 *   visibility (read by ASOLFlightHud per frame).
 * - On Apply: vsync and the quality preset (and resolution and window mode, outside the editor) go to
 *   UGameUserSettings and are applied; the key bindings become the applied ones and OnKeyBindingsChanged fires; the
 *   default flight assist is read when the ship spawns.
 *
 * Persistence follows the project standard: the save slot is read once here at boot; Apply and the setters only change
 * memory; EndVisit (the Settings page or the menu closing) reverts what was not applied and writes once, with an async
 * USaveGame write only if an Apply changed something since the last write (CommitIfDirty) and one
 * UGameUserSettings::SaveSettings only if the engine settings changed. A failed async write is owed again; Deinitialize
 * (shutdown) writes synchronously if a write is still owed or an async one has not reported back. At boot only the vsync
 * and quality preset the player chose on the page are pushed to the engine (SOLSettingsRules::DecideBootGraphics), so
 * an engine "custom" scalability mix is never overwritten. Resolution and window mode live in GameUserSettings.ini,
 * which the engine reads at start.
 *
 * Key rebinding stores overrides in the same save (not Enhanced Input user settings: the ship's mapping context is a
 * transient object the pawn builds in C++, see SDD 8 Amendment 2). -SOLMenuSmoke runs use their own slot, deleted at
 * start, so a verification run never touches the player's settings file.
 */
UCLASS()
class SOLTEST_API USOLSettingsSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:

    // Picks the save slot, loads it once, resolves the keys and loads the sound classes
    virtual void Initialize(FSubsystemCollectionBase& collection) override;

    // Writes synchronously if a write is still owed or in flight (shutdown), then lets go of everything
    virtual void Deinitialize() override;

    // Returns the settings subsystem of an object's game instance, or nullptr
    static USOLSettingsSubsystem* Get(const UObject* worldContext);

    // Returns the live values (pending: what previews and what the game uses now)
    const SOLSettingsModel::FSettingsValues& GetLiveValues() const { return mModel.Pending; }

    // Returns the applied (committed) values
    const SOLSettingsModel::FSettingsValues& GetSavedValues() const { return mModel.Saved; }

    // Sets the pending values (sanitized by the model) and previews them
    void SetPendingValues(const SOLSettingsModel::FSettingsValues& values);

    // Returns the resolutions offered (built on first use from the display's list plus the current one)
    const TArray<FIntPoint>& GetResolutionChoices();

    // Returns the pending resolution
    const FIntPoint& GetPendingResolution() const { return mPendingResolution; }

    // Sets the pending resolution (applied on Apply)
    void SetPendingResolution(const FIntPoint& resolution);

    // Returns the pending window mode (EWindowMode::Type value)
    int32 GetPendingWindowMode() const { return mPendingWindowMode; }

    // Sets the pending window mode (clamped; applied on Apply)
    void SetPendingWindowMode(int32 mode);

    // Returns the pending keys (indexed by SOLKeyBindings::EBinding)
    const TArray<FString>& GetPendingKeys() const { return mPendingKeys; }

    // Returns the applied keys the ship pawn maps
    const TArray<FString>& GetAppliedKeys() const { return mAppliedKeys; }

    // Rebinds one pending key (SOLKeyBindings::Rebind); outConflict is another binding with the same key
    SOLKeyBindings::ERebindResult RebindKey(int32 binding, const FString& keyName, int32& outConflict);

    // Sets every pending key back to its default
    void ResetKeysToDefaults();

    // Returns whether anything pending differs from what is applied
    bool IsDirty() const;

    // Returns whether the world-space orbit lines are shown (the pending value, so the Settings page previews live)
    bool AreOrbitLinesShown() const { return mModel.Pending.bShowOrbitLines; }

    // Sets the applied (and pending) orbit-line flag from the O key in flight; the slot is written at the next write
    // point (EndVisit or shutdown), and only if the value then differs from what is on disk
    void SetOrbitLinesShown(bool bShown);

    // Applies everything pending; returns true if anything changed (the write happens at EndVisit)
    bool ApplyPending();

    // Discards everything pending and previews the applied values again
    void RevertPending();

    // Ends a settings visit (page or menu closed): reverts what was not applied, then writes once if owed
    void EndVisit();

    // Starts the owed writes: one async save of the slot and/or one GameUserSettings save
    void FlushSave();

    // Pushes the volume mix on a newly begun world and applies the volumes there
    void ApplyToWorld(UWorld& world);

    // Pops the volume mix from a world that is being torn down (pushed by ApplyToWorld); other worlds are ignored
    void ReleaseWorld(UWorld& world);

    // Marks whether a Settings page is listening for a rebind key (menu buttons do not take focus on hover meanwhile)
    void SetKeyCaptureActive(bool bActive) { mIsKeyCaptureActive = bActive; }

    // Returns true while a Settings page is listening for a rebind key
    bool IsKeyCaptureActive() const { return mIsKeyCaptureActive; }

    // Fired after the applied key bindings change
    FSOLOnKeyBindingsChanged& OnKeyBindingsChanged() { return mOnKeyBindingsChanged; }

    // Returns the save slot in use
    const FString& GetSlotName() const { return mSlotName; }

    // Returns the absolute path of the save slot's file (verification)
    FString GetSlotFilePath() const;

    // Returns how many slot writes were started (verification)
    int32 GetSaveRequestCount() const { return mSaveRequestCount; }

    // Returns how many slot writes finished successfully (verification)
    int32 GetSaveCompletedCount() const { return mSaveCompletedCount; }

    // Returns how many GameUserSettings writes were made (verification)
    int32 GetEngineSettingsWriteCount() const { return mEngineSettingsWriteCount; }

    // Reads the slot from disk again, as a restart would (verification); false if there is no usable save
    bool ReadSlotForVerification(SOLSettingsModel::FSettingsValues& outValues, TArray<FString>& outKeys) const;

    // Asks the audio thread for the effects class's current mixed volume (verification; read it a frame later)
    void ProbeEffectsVolume();

    // Returns the last probed effects volume (-1 before a probe arrived or without an audio device)
    float GetProbedEffectsVolume() const { return *mProbedEffectsVolume; }

private:

    // Reads the slot once: uses, migrates or ignores it by version, and seeds what it does not hold from the engine
    void LoadOnce();

    // Upgrades an older save to the current schema in memory (no older schema exists yet)
    static void MigrateSave(USOLSettingsSave& save);

    // Reads the applied resolution and window mode from GameUserSettings
    void SeedEngineGraphics();

    // Pushes the chosen applied values (vsync, quality preset, resolution and window mode) into GameUserSettings,
    // applying only what differs from the engine; returns true if anything changed
    bool PushEngineGraphics(bool bVsync, bool bQuality, bool bResolution);

    // Sets the volume overrides from the live values on the audio world
    void ApplyAudio();

    // Async save finished: a failure makes the write owed again
    void HandleSaveCompleted(const FString& slotName, int32 userIndex, bool bSuccess);

    // Copies the applied values, keys and chosen flags into the save object
    void FillSaveObject();

    // Returns whether the slot needs a write (an Apply or migration owed one, or the orbit-line flag differs from disk)
    bool IsSaveOwed() const;

    UPROPERTY(Transient)
    TObjectPtr<USOLSettingsSave> SaveObject;

    UPROPERTY(Transient)
    TObjectPtr<USoundMix> VolumeMix;

    UPROPERTY(Transient)
    TObjectPtr<USoundClass> MasterSoundClass;

    UPROPERTY(Transient)
    TObjectPtr<USoundClass> SfxSoundClass;

    UPROPERTY(Transient)
    TObjectPtr<USoundClass> MusicSoundClass;

    SOLSettingsModel::FSettingsModel mModel;            // Saved (applied) and pending values
    TArray<FString> mAppliedKeys;                       // Keys the ship maps (one per binding)
    TArray<FString> mPendingKeys;                       // Keys being edited
    TArray<FIntPoint> mResolutionChoices;               // Offered resolutions (built on first use)
    FIntPoint mAppliedResolution = FIntPoint::ZeroValue;
    FIntPoint mPendingResolution = FIntPoint::ZeroValue;
    int32 mAppliedWindowMode = 0;                       // EWindowMode::Type value
    int32 mPendingWindowMode = 0;
    TWeakObjectPtr<UWorld> mAudioWorld;                 // World whose audio device has the volume mix
    TSharedRef<float, ESPMode::ThreadSafe> mProbedEffectsVolume = MakeShared<float, ESPMode::ThreadSafe>(-1.0f);
    FString mSlotName;                                  // Save slot in use
    FSOLOnKeyBindingsChanged mOnKeyBindingsChanged;
    int32 mSaveRequestCount = 0;
    int32 mSaveCompletedCount = 0;
    int32 mEngineSettingsWriteCount = 0;
    int32 mSaveWritesInFlight = 0;                      // Async slot writes started and not yet reported back
    bool mIsSaveOwed = false;                           // An Apply changed the slot's content since the last write (or
                                                        // the last write failed)
    bool mWrittenShowOrbitLines = true;                 // bShowOrbitLines as on disk (loaded or last written)
    bool mIsVsyncChosen = false;                        // The player changed vsync on the page (pushed at boot)
    bool mIsQualityChosen = false;                      // The player changed the quality preset (pushed at boot)
    bool mIsKeyCaptureActive = false;                   // A Settings page is listening for a rebind key
    bool mIsEngineSettingsSaveOwed = false;             // GameUserSettings changed since its last write
    bool mHasResolutionChoices = false;
};
