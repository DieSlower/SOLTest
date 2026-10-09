/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Combat/SOLCombatAudio.h"

#include "Combat/SOLCombatRules.h"
#include "Combat/SOLCombatSubsystem.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLRenderOrigin.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

static_assert(UE_ARRAY_COUNT(SOL::COMBAT_SOUNDS) == static_cast<int32>(ESOLCombatSound::Count),
    "SOL::COMBAT_SOUNDS needs exactly one entry per ESOLCombatSound kind");

namespace
{
    // Names of the sound kinds, in ESOLCombatSound order (component names and the end-of-play summary)
    const TCHAR* const COMBAT_SOUND_NAMES[] =
        { TEXT("Shot"), TEXT("ShieldHit"), TEXT("HullHit"), TEXT("ShieldBreak"), TEXT("Explosion"), TEXT("Spark"),
          TEXT("TargetDrop") };
    static_assert(UE_ARRAY_COUNT(COMBAT_SOUND_NAMES) == static_cast<int32>(ESOLCombatSound::Count),
        "COMBAT_SOUND_NAMES needs exactly one entry per ESOLCombatSound kind");
}

//////////////////////////////////////////////////////////////////////////
// Creates the scene root
ASOLCombatAudio::ASOLCombatAudio()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;
}

//////////////////////////////////////////////////////////////////////////
// Loads the sounds, builds the attenuation settings and voice pools once, and subscribes to the combat update
void ASOLCombatAudio::BeginPlay()
{
    Super::BeginPlay();

    UWorld* world = GetWorld();
    Combat = world->GetSubsystem<USOLCombatSubsystem>();
    Anchor = world->GetSubsystem<USOLAnchorSubsystem>();
    Ships = world->GetSubsystem<USOLShipSubsystem>();
    if (Combat == nullptr || Anchor == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("CombatAudio %s: combat or anchor subsystem missing; nothing will be heard"),
            *GetName());
        return;
    }

    Attenuation = MakeAttenuation(SOL::COMBAT_AUDIO_INNER_RADIUS_CM, SOL::COMBAT_AUDIO_FALLOFF_CM);
    LongRangeAttenuation = MakeAttenuation(SOL::COMBAT_AUDIO_LONG_INNER_RADIUS_CM, SOL::COMBAT_AUDIO_LONG_FALLOFF_CM);

    // One pool of pre-registered voices per kind, each already holding its sound and attenuation
    int32 voiceTotal = 0;
    for (const SOL::FSOLCombatSoundTuning& tuning : SOL::COMBAT_SOUNDS)
    {
        voiceTotal += tuning.Voices;
    }
    VoiceComponents.Reserve(voiceTotal);
    for (int32 kindIndex = 0; kindIndex < static_cast<int32>(ESOLCombatSound::Count); ++kindIndex)
    {
        const SOL::FSOLCombatSoundTuning& tuning = SOL::COMBAT_SOUNDS[kindIndex];
        USoundBase* sound = LoadObject<USoundBase>(nullptr, tuning.Path);
        if (sound == nullptr)
        {
            UE_LOG(LogSOL, Warning, TEXT("CombatAudio %s: %s is missing; that sound is disabled"), *GetName(),
                tuning.Path);
            continue;
        }

        FSOLSoundKind& kind = mKinds[kindIndex];
        kind.DurationS = sound->GetDuration();
        kind.Voices.SetNum(tuning.Voices);
        for (int32 voiceIndex = 0; voiceIndex < tuning.Voices; ++voiceIndex)
        {
            UAudioComponent* component = NewObject<UAudioComponent>(this,
                FName(*FString::Printf(TEXT("%sSfx%d"), COMBAT_SOUND_NAMES[kindIndex], voiceIndex)));
            component->SetupAttachment(SceneRoot);
            component->SetUsingAbsoluteLocation(true);
            component->bAutoActivate = false;
            component->bAutoDestroy = false;
            component->bAllowSpatialization = true;
            component->AttenuationSettings = tuning.bIsLongRange ? LongRangeAttenuation : Attenuation;
            component->SetVolumeMultiplier(tuning.Volume);
            component->SetSound(sound);
            component->RegisterComponent();
            kind.Voices[voiceIndex].Component = component;
            VoiceComponents.Add(component);
        }
    }

    mCombatUpdatedHandle = Combat->OnCombatUpdated().AddUObject(this, &ASOLCombatAudio::HandleCombatUpdated);
    UE_LOG(LogSOL, Log, TEXT("CombatAudio %s: %d pooled voices"), *GetName(), VoiceComponents.Num());
}

//////////////////////////////////////////////////////////////////////////
// Unsubscribes from the combat update and logs how many sounds of each kind were played and dropped
void ASOLCombatAudio::EndPlay(const EEndPlayReason::Type endPlayReason)
{
    if (Combat != nullptr)
    {
        Combat->OnCombatUpdated().Remove(mCombatUpdatedHandle);
    }
    mCombatUpdatedHandle.Reset();

    // One summary line (not one per sound): what a verification run checks that every kind was requested
    FString summary;
    for (int32 kindIndex = 0; kindIndex < static_cast<int32>(ESOLCombatSound::Count); ++kindIndex)
    {
        summary += FString::Printf(TEXT(" %s=%d/%d"), COMBAT_SOUND_NAMES[kindIndex], mKinds[kindIndex].PlayedCount,
            mKinds[kindIndex].DroppedCount);
    }
    UE_LOG(LogSOL, Log, TEXT("CombatAudio %s: sounds played/dropped:%s"), *GetName(), *summary);
    Super::EndPlay(endPlayReason);
}

//////////////////////////////////////////////////////////////////////////
// Creates a sound attenuation object with the given inner radius and falloff (render cm)
USoundAttenuation* ASOLCombatAudio::MakeAttenuation(const float innerRadiusCm, const float falloffCm)
{
    USoundAttenuation* attenuation = NewObject<USoundAttenuation>(this);
    FSoundAttenuationSettings& settings = attenuation->Attenuation;
    settings.bAttenuate = true;
    settings.bSpatialize = true;
    settings.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
    settings.AttenuationShape = EAttenuationShape::Sphere;
    settings.AttenuationShapeExtents = FVector(innerRadiusCm, 0.0, 0.0);
    settings.FalloffDistance = falloffCm;
    return attenuation;
}

//////////////////////////////////////////////////////////////////////////
// Returns the player ship's universe position (ecliptic), or the origin without a ship
FVector3d ASOLCombatAudio::GetObserverPositionM() const
{
    return Ships != nullptr && Ships->HasPlayerShip() ? Ships->GetUniversePositionM() : FVector3d::ZeroVector;
}

//////////////////////////////////////////////////////////////////////////
// Starts this frame's event sounds and moves the live voices; bound to OnCombatUpdated
void ASOLCombatAudio::HandleCombatUpdated()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLCombatAudio::HandleCombatUpdated);
    const UWorld* world = GetWorld();
    const double nowS = world->GetTimeSeconds();
    const TConstArrayView<float> shields = Combat->GetTargetShieldFractions();
    for (const FSOLCombatEvent& event : Combat->GetEvents())
    {
        // Same shield test as the impact tint: blue ping while the shield holds, hull thud once it is down
        const bool bShielded = shields.IsValidIndex(event.TargetSlot) && shields[event.TargetSlot] > 0.0f;
        const ESOLCombatSound sound = SOLCombatAudioRules::SoundForEvent(event.Type, bShielded);
        if (sound == ESOLCombatSound::None)
        {
            continue;
        }
        // The player's own shots ride with the ship (the listener), not with the bolt's frame body
        const bool bIsPlayerShot = event.Type == ESOLCombatEventType::BoltFired
            && event.Owner == SOLCombat::PLAYER_OWNER;
        PlaySound(sound, event, bIsPlayerShot, nowS);
    }
    UpdateVoices(Anchor->GetRenderOrigin(), Combat->GetLastUpdateDeltaS());
}

//////////////////////////////////////////////////////////////////////////
// Starts one sound of a kind at an event (unless the kind's minimum interval drops it)
void ASOLCombatAudio::PlaySound(const ESOLCombatSound sound, const FSOLCombatEvent& event, const bool bRidesWithShip,
    const double nowS)
{
    const int32 kindIndex = static_cast<int32>(sound);
    FSOLSoundKind& kind = mKinds[kindIndex];
    if (kind.Voices.IsEmpty())
    {
        return;
    }
    if (!SOLCombatAudioRules::MayPlay(kind.LastPlayS, nowS, SOL::COMBAT_SOUNDS[kindIndex].MinIntervalS))
    {
        ++kind.DroppedCount;
        return;
    }
    kind.LastPlayS = nowS;
    ++kind.PlayedCount;

    // Reuse the oldest voice of this kind (stopping it if it is still sounding)
    FSOLVoice& voice = kind.Voices[kind.NextVoice];
    kind.NextVoice = (kind.NextVoice + 1) % kind.Voices.Num();
    voice.bRidesWithShip = bRidesWithShip;
    voice.FrameBody = event.FrameBody;
    voice.FrameOffsetM = bRidesWithShip ? event.PositionM - GetObserverPositionM() : event.FrameOffsetM;
    voice.RelativeVelocityMps = bRidesWithShip ? FVector3d::ZeroVector : event.RelativeVelocityMps;
    voice.AgeS = 0.0;
    voice.bIsLive = true;

    UAudioComponent* component = voice.Component;
    component->SetWorldLocation(FVector(Anchor->GetRenderOrigin().UniverseToRenderCm(event.PositionM)));
    component->Play();
    UE_LOG(LogSOL, Verbose, TEXT("CombatAudio: %s at %s"), COMBAT_SOUND_NAMES[kindIndex],
        *component->GetComponentLocation().ToString());
}

//////////////////////////////////////////////////////////////////////////
// Moves every live voice with its frame (or the ship) and retires the ones whose sound has ended
void ASOLCombatAudio::UpdateVoices(const FSOLRenderOrigin& origin, const double deltaSeconds)
{
    const FVector3d shipPositionM = GetObserverPositionM();
    for (FSOLSoundKind& kind : mKinds)
    {
        for (FSOLVoice& voice : kind.Voices)
        {
            if (!voice.bIsLive)
            {
                continue;
            }
            voice.AgeS += deltaSeconds;
            if (voice.AgeS > kind.DurationS)
            {
                // The one-shot sound has finished on its own by now
                voice.bIsLive = false;
                continue;
            }
            // Recomputed from where its frame (or the ship) is now, so time warp's carry is followed exactly
            const FVector3d anchorM = voice.bRidesWithShip ? shipPositionM
                : Combat->GetFrameBodyPositionM(voice.FrameBody);
            const FVector3d positionM = SOLCombatRules::EventPositionNowM(anchorM, voice.FrameOffsetM,
                voice.RelativeVelocityMps, voice.AgeS);
            voice.Component->SetWorldLocation(FVector(origin.UniverseToRenderCm(positionM)));
        }
    }
}
