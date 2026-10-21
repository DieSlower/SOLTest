/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SOLNpcCombatVisuals.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class USOLAnchorSubsystem;
class USOLNpcSubsystem;

// One pooled NPC effect slot (an explosion or an impact riding with the NPC combat frame body)
USTRUCT()
struct FSOLNpcEffectSlot
{
    GENERATED_BODY()

    UPROPERTY(Transient)
    TObjectPtr<UNiagaraComponent> Component;

    FVector3d FrameOffsetM = FVector3d::ZeroVector;   // Position relative to the NPC combat frame body
    double AgeS = 0.0;
    bool bIsLive = false;
};

// A ring of pooled NPC effect slots of one Niagara system
USTRUCT()
struct FSOLNpcEffectPool
{
    GENERATED_BODY()

    UPROPERTY(Transient)
    TArray<FSOLNpcEffectSlot> Slots;

    double DurationS = 0.0;
    int32 NextSlot = 0;
};

/**
 * Draws NPC-vs-NPC combat (SDD 11 section 3.4 and 3.6, Part 10d): the NPC bolts near the camera through their own
 * instance of the bolt Niagara system (NS_SOLBolts, fed from arrays like ASOLCombatVisuals, separate from the player's
 * bolts), and pooled NPC explosions and impacts near the camera, separate from the player's combat pools. Redrawn on
 * USOLNpcSubsystem::OnNpcsUpdated. Not ticking.
 */
UCLASS()
class SOLTEST_API ASOLNpcCombatVisuals : public AActor
{
    GENERATED_BODY()

public:

    // Creates the root and the bolt Niagara component
    ASOLNpcCombatVisuals();

protected:

    // Loads the systems, builds the pools and subscribes to NPC updates
    virtual void BeginPlay() override;

    // Unsubscribes from NPC updates
    virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

private:

    // Redraws the bolts and starts and moves the effects; bound to OnNpcsUpdated
    void Redraw();

    // Feeds the bolts near the camera to the bolt system
    void UpdateBolts();

    // Starts this frame's effects and moves the live ones
    void UpdateEffects(double deltaS);

    // Creates a pool of components for a system
    void CreatePool(FSOLNpcEffectPool& pool, UNiagaraSystem* system, int32 size, double durationS, const TCHAR* name);

    // Starts one effect of a pool at a frame-relative position
    void StartEffect(FSOLNpcEffectPool& pool, const FVector3d& frameOffsetM, const FVector3d& directionEcliptic);

    UPROPERTY(VisibleAnywhere, Category = "SOL|Npc")
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, Category = "SOL|Npc")
    TObjectPtr<UNiagaraComponent> BoltNiagara;

    UPROPERTY(Transient)
    FSOLNpcEffectPool ExplosionPool;

    UPROPERTY(Transient)
    FSOLNpcEffectPool ImpactPool;

    UPROPERTY(Transient)
    TObjectPtr<USOLNpcSubsystem> Npcs;

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> Anchor;

    FDelegateHandle mNpcsUpdatedHandle;
    TArray<double> mBoltDistancesSq;                  // Scratch: squared distances of the bolts in range
    TArray<FVector3f> mBoltPositionsCm;               // Scratch, reserved once to the render cap
    TArray<FVector3f> mBoltVelocitiesCmps;
    bool mIsBoltRendererEnabled = false;
    bool mIsBoltRendererRunning = false;
};
