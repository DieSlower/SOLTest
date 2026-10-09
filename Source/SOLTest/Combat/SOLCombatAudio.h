/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Combat/SOLCombatAudioRules.h"

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SOLCombatAudio.generated.h"

class UAudioComponent;
class USOLAnchorSubsystem;
class USOLCombatSubsystem;
class USOLShipSubsystem;
class USoundAttenuation;
struct FSOLRenderOrigin;

/**
 * Placeholder combat audio (SDD 7, Part 7d). Spawned once by ASOLGameMode next to ASOLCombatVisuals and, like it, driven
 * by USOLCombatSubsystem::OnCombatUpdated with no tick of its own.
 *
 * - One fixed pool of pre-registered UAudioComponents per sound kind (SOL::COMBAT_SOUNDS), each set to its sound and
 *   attenuation once; a new sound reuses the oldest component of its kind, so a kind never has more voices than its
 *   pool (a "stop oldest" concurrency limit without a concurrency asset).
 * - A per-kind minimum interval (SOLCombatAudioRules::MayPlay) drops identical sounds that would start in the same frame
 *   or a few milliseconds apart.
 * - Sounds are 3D: placed at the event's universe position converted with the frame's render-origin snapshot, and
 *   re-placed each update on what they happened on (recomputed from the event's frame body, FSOLCombatEvent, so time
 *   warp's carry is followed exactly; the player's own shots keep their offset from the ship) until the sound ends, so
 *   neither the ship's motion, time warp nor an origin rebase leaves a sound behind. Voices age with the combat
 *   update's own (hitch-clamped) delta.
 *
 * Every sound asset is optional: a missing one silences only its kind, with one warning.
 */
UCLASS()
class SOLTEST_API ASOLCombatAudio : public AActor
{
    GENERATED_BODY()

public:

    // Creates the scene root
    ASOLCombatAudio();

protected:

    // Loads the sounds, builds the attenuation settings and voice pools once, and subscribes to the combat update
    virtual void BeginPlay() override;

    // Unsubscribes from the combat update and logs how many sounds of each kind were played and dropped
    virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

private:

    // One pooled voice and where its sound is anchored in the universe
    struct FSOLVoice
    {
        TObjectPtr<UAudioComponent> Component;
        int32 FrameBody = INDEX_NONE;                          // The event's frame body
        FVector3d FrameOffsetM = FVector3d::ZeroVector;        // Offset from that body (or from the ship, see below)
        FVector3d RelativeVelocityMps = FVector3d::ZeroVector; // Velocity relative to that body
        double AgeS = 0.0;
        bool bIsLive = false;
        bool bRidesWithShip = false;                           // The player's own shot: FrameOffsetM is from the ship
    };

    // The voices and play state of one sound kind
    struct FSOLSoundKind
    {
        TArray<FSOLVoice> Voices;      // Empty when the kind's sound is missing
        int32 NextVoice = 0;           // Oldest started voice: the next one reused
        double DurationS = 0.0;        // Sound length: how long a voice is kept riding along
        double LastPlayS = SOLCombatAudioRules::NEVER_PLAYED_S;
        int32 PlayedCount = 0;         // Diagnostics: sounds started
        int32 DroppedCount = 0;        // Diagnostics: sounds dropped by the minimum interval
    };

    // Starts this frame's event sounds and moves the live voices; bound to OnCombatUpdated
    void HandleCombatUpdated();

    // Starts one sound of a kind at an event (unless the kind's minimum interval drops it); a player's own shot rides
    // with the ship, anything else with the event's frame body
    void PlaySound(ESOLCombatSound sound, const FSOLCombatEvent& event, bool bRidesWithShip, double nowS);

    // Moves every live voice with its frame (or the ship) and retires the ones whose sound has ended
    void UpdateVoices(const FSOLRenderOrigin& origin, double deltaSeconds);

    // Creates a sound attenuation object with the given inner radius and falloff (render cm)
    USoundAttenuation* MakeAttenuation(float innerRadiusCm, float falloffCm);

    // Returns the player ship's universe position (ecliptic), or the origin without a ship
    FVector3d GetObserverPositionM() const;

    /** Scene root at the Unreal origin. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Audio")
    TObjectPtr<USceneComponent> SceneRoot;

    /** Every pooled voice, held here so the kinds' raw voice slots stay referenced for GC. */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UAudioComponent>> VoiceComponents;

    /** Normal and long-range (explosion) attenuation, built once from SOLConstants. */
    UPROPERTY(Transient)
    TObjectPtr<USoundAttenuation> Attenuation;

    UPROPERTY(Transient)
    TObjectPtr<USoundAttenuation> LongRangeAttenuation;

    UPROPERTY(Transient)
    TObjectPtr<USOLCombatSubsystem> Combat;

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> Anchor;

    UPROPERTY(Transient)
    TObjectPtr<USOLShipSubsystem> Ships;

    FSOLSoundKind mKinds[static_cast<int32>(ESOLCombatSound::Count)];

    FDelegateHandle mCombatUpdatedHandle;
};
