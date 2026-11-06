/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLEngineSettingsSnapshot.h"
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
 * UGameUserSettings::SaveSettings only if the engine settings changed (never during a smoke run, which gives the boot
 * values back at shutdown). A failed async write is owed again; Deinitialize
 * (shutdown) writes synchronously if a write is still owed or an async one has not reported back. At boot only the vsync
 * and quality preset the player chose on the page are pushed to the engine (SOLSettingsRules::DecideBootGraphics), so
 * an engine "custom" scalability mix is never overwritten. Resolution and window mode live in GameUserSettings.ini,
 * which the engine reads at start.
 *
 * Key rebinding stores overrides in the same save (not Enhanced Input user settings: the ship's mapping context is a
 * transient object the pawn builds in C++, see SDD 8 Amendment 2). -SOLMenuSmoke runs use their own slot, deleted at
 * start, so a verification run never touches the player's settings file; they also copy the engine's GameUserSettings
 * at boot (FSOLEngineSettingsSnapshot) and write them back in Deinitialize, after the engine's own exit write, so the
 * run's window sizes never reach the player's GameUserSettings.ini (SDD 46 section 14, part 46i).
 */
UCLASS()
class SOLTEST_API USOLSettingsSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:

    // Picks the save slot, loads it once, resolves the keys and loads the sound classes
    virtual void Initialize(FSubsystemCollectionBase& collection) override;

    // Writes synchronously if a write is still owed or in flight (shutdown; a smoke run gives the engine's
    // GameUserSettings back), then lets go of everything
    virtual void Deinitialize() override;

    // Returns the settings subsystem of an object's game instance, or nullptr
    static USOLSettingsSubsystem* Get(const UObject* worldContext);

    // Returns the live values (pending: what previews and what the game uses now)
    const SOLSettingsModel::FSettingsValues& GetLiveValues() const { return mModel.Pending; }

    // Returns the applied (committed) values
    const SOLSettingsModel::FSettingsValues& GetSavedValues() const { return mModel.Saved; }

    // Returns the applied Chase Down safety margin (SDD 32 section 3.10; a pending edit counts only after Apply), read
    // by the ship subsystem when it plans a chase
    float GetChaseSafetyMargin() const { return mModel.Saved.ChaseSafetyMargin; }

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

    // Returns true once (then false) when the loaded save predates the current default key layout, so the HUD shows
    // its one-time "Default controls changed" notice (SDD 19 Q11)
    bool ConsumeControlsChangedNotice();

    // Rebinds one pending key (SOLKeyBindings::Rebind); outConflict is another binding with the same key
    SOLKeyBindings::ERebindResult RebindKey(int32 binding, const FString& keyName, int32& outConflict);

    // Sets every pending key back to its default
    void ResetKeysToDefaults();

    // Returns whether anything pending differs from what is applied
    bool IsDirty() const;

    // Returns whether the active overlay profile's orbit-lines switch (Alt+O) is on (SDD 34 D18)
    bool AreOrbitLinesShown() const { return SOLOverlay::AreOrbitLinesOn(mModel.Saved.OverlayPrefs); }

    // Sets the active overlay profile's orbit-lines switch (Alt+O), applied at once; the slot is written at the next
    // write point, and only if the prefs then differ from what is on disk
    void SetOrbitLinesShown(bool bShown);

    // Returns the overlay choices: the applied prefs, which every overlay read and write uses (an overlay change is
    // applied at once, never a pending edit, so the pending copy only mirrors them)
    const SOLOverlay::FOverlayPrefs& GetOverlayPrefs() const { return mModel.Saved.OverlayPrefs; }

    // Sets the applied (and pending) overlay choices from the Overlay Menu; written at the next write point (the
    // Overlay Menu's close, EndVisit, the ship pawn's EndPlay or shutdown) and only if they then differ from disk
    void SetOverlayPrefs(const SOLOverlay::FOverlayPrefs& prefs);

    // Flips one row of the active overlay profile (applied at once) and returns its new state
    bool ToggleOverlay(SOLOverlay::EOverlay overlay);

    // Returns whether the active overlay profile's master switch (O) is on (saved with the profile, SDD 34 D10)
    bool IsOverlayMasterOn() const { return SOLOverlay::IsMasterOn(mModel.Saved.OverlayPrefs); }

    // Sets the active overlay profile's master switch (O), applied at once; written at the next write point
    void SetOverlayMasterOn(bool bOn);

    // Makes the next enabled overlay profile active (RShift+O) and returns it; written at the next write point
    int32 CycleOverlayProfile();

    // Makes an enabled overlay profile active; returns whether the active profile changed (written at the next write
    // point)
    bool SetActiveOverlayProfile(int32 profile);

    // Enables or disables overlay profile 1-4 (SOLOverlay::SetProfileEnabled); returns whether anything changed
    // (written at the next write point)
    bool SetOverlayProfileEnabled(int32 profile, bool bEnabled);

    // Returns whether the space dust speed cues are on (the applied Settings value, SDD 34 section 9)
    bool IsSpaceDustOn() const { return mModel.Saved.bSpaceDust; }

    // Returns whether an overlay shows now: the active profile's master switch and rows (SOLOverlay::IsShown)
    bool IsOverlayShown(SOLOverlay::EOverlay overlay) const;

    // Returns whether an orbit category's lines show: IsOverlayShown and the active profile's orbit-lines switch
    bool IsOverlayOrbitShown(SOLOverlay::EOverlay overlay) const;

    // Returns whether the player has steered with the mouse since the game started (session only, never saved; the HUD
    // shows its "Hold Space to steer" hint until then, issue #31)
    bool HasSteeredThisSession() const { return mHasSteeredThisSession; }

    // Records that the player steered with the mouse this session
    void MarkSteeredThisSession() { mHasSteeredThisSession = true; }

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

    // Upgrades an older save to the current schema in memory (USOLSettingsSave::MigrateToCurrentVersion)
    static void MigrateSave(USOLSettingsSave& save);

    // Reads the applied resolution and window mode from GameUserSettings
    void SeedEngineGraphics();

    // A smoke run: writes the engine's GameUserSettings back as they were at boot (not applied) and saves them once
    void RestoreEngineSettingsAfterSmoke();

    // Pushes the chosen applied values (vsync, quality preset, resolution and window mode) into GameUserSettings,
    // applying only what differs from the engine; returns true if anything changed
    bool PushEngineGraphics(bool bVsync, bool bQuality, bool bResolution);

    // Sets the volume overrides from the live values on the audio world
    void ApplyAudio();

    // Async save finished: a failure makes the write owed again
    void HandleSaveCompleted(const FString& slotName, int32 userIndex, bool bSuccess);

    // Copies the applied values, keys and chosen flags into the save object
    void FillSaveObject();

    // Returns whether the slot needs a write (an Apply or migration owed one, or the overlay prefs differ from disk)
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
    FSOLEngineSettingsSnapshot mSmokeEngineSettings;    // A smoke game run: the engine's GameUserSettings at boot
                                                        // (else empty; also blocks mid-run GameUserSettings writes)
    FString mSlotName;                                  // Save slot in use
    FSOLOnKeyBindingsChanged mOnKeyBindingsChanged;
    int32 mSaveRequestCount = 0;
    int32 mSaveCompletedCount = 0;
    int32 mEngineSettingsWriteCount = 0;
    int32 mSaveWritesInFlight = 0;                      // Async slot writes started and not yet reported back
    bool mIsControlsChangedNoticePending = false;       // The loaded save predates the current default key layout
    bool mIsSaveOwed = false;                           // An Apply changed the slot's content since the last write (or
                                                        // the last write failed)
    SOLOverlay::FOverlayPrefs mWrittenOverlayPrefs = SOLOverlay::MakeDefaultPrefs();   // Overlay prefs as on disk
    bool mIsVsyncChosen = false;                        // The player changed vsync on the page (pushed at boot)
    bool mIsQualityChosen = false;                      // The player changed the quality preset (pushed at boot)
    bool mIsKeyCaptureActive = false;                   // A Settings page is listening for a rebind key
    bool mHasSteeredThisSession = false;                // Mouse steering was on at least once this session
    bool mIsEngineSettingsSaveOwed = false;             // GameUserSettings changed since its last write
    bool mHasResolutionChoices = false;
};
