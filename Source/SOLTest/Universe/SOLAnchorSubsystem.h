/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "Universe/SOLAnchor.h"
#include "Universe/SOLRenderOrigin.h"
#include "Universe/SOLRenderPlacement.h"

#include "SOLAnchorSubsystem.generated.h"

class USOLBodyRegistrySubsystem;
class USOLSimClockSubsystem;

DECLARE_MULTICAST_DELEGATE(FSOLOnUniverseUpdated);
DECLARE_MULTICAST_DELEGATE_OneParam(FSOLOnBodiesUpdated, float /*realDeltaSeconds*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FSOLOnRenderOriginShifted, const FVector& /*shiftCm*/);

/**
 * Owns the observer's double-precision universe position, the anchor (nearest body) and the render origin, and drives
 * the per-frame universe update: clock, then registry, then OnBodiesUpdated (ship step, observer sync), then anchor
 * and rebase, then OnUniverseUpdated (body visuals).
 *
 * Rendering is observer-relative: the render origin is a universe point (meters, double) that maps to Unreal (0,0,0).
 * It snaps to the observer whenever the anchor changes or the observer drifts RENDER_REBASE_DISTANCE_M from it, so
 * Unreal-space coordinates stay small. UWorld::SetNewWorldOrigin is not used: its FIntVector origin is int32 cm
 * (about +-21,000 km), far too small for AU-scale positions, and bodies are re-placed from the universe every frame.
 * The render-origin arithmetic itself lives in FSOLRenderOrigin (unit-tested).
 */
UCLASS()
class SOLTEST_API USOLAnchorSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:

    // Resolves the clock and registry dependencies and picks the initial anchor
    virtual void Initialize(FSubsystemCollectionBase& collection) override;

    // Drops the observer actor reference
    virtual void Deinitialize() override;

    // Advances the universe one frame: clock, registry, ship step, anchor, rebase, then notifies listeners
    virtual void Tick(float deltaTime) override;

    // Returns the stat id used by the tickable-object profiler
    virtual TStatId GetStatId() const override;

    // Teleports the observer (meters, ecliptic): re-picks the nearest body as anchor and rebases the render origin
    void SetObserverPositionM(const FVector3d& positionM);

    // Moves the observer by a universe-space delta (meters, ecliptic)
    void MoveObserverM(const FVector3d& deltaM);

    // Sets the observer's position for continuous motion (meters, ecliptic): anchor hysteresis and rebasing still apply
    void SyncObserverPositionM(const FVector3d& positionM) { mObserverPositionM = positionM; }

    // Returns the observer's universe position (meters, ecliptic)
    const FVector3d& GetObserverPositionM() const { return mObserverPositionM; }

    // Registers the actor that represents the observer; it is moved when the render origin shifts
    void SetObserverActor(AActor* observerActor);

    // Returns the current anchor body index, or INDEX_NONE before the first update
    int32 GetAnchorIndex() const { return mAnchor.GetAnchorIndex(); }

    // Returns the anchor body's universe position (meters), or the Sun-frame origin if there is no anchor
    FVector3d GetAnchorPositionM() const;

    // Returns the universe point that maps to Unreal (0,0,0)
    const FVector3d& GetRenderOriginM() const { return mRenderOrigin.OriginM; }

    // Converts a universe position (meters, ecliptic) to an Unreal render location (cm, Unreal axes)
    FVector UniverseToRenderCm(const FVector3d& universeM) const;

    // Converts an Unreal render location (cm, Unreal axes) to a universe position (meters, ecliptic)
    FVector3d RenderToUniverseM(const FVector& renderCm) const;

    // Returns a body's render location and radius (cm, Unreal axes); 1:1 near the observer, angular size kept far away
    FSOLRenderPlacement ComputeBodyRenderPlacement(int32 bodyIndex) const;

    // Returns a universe point's render location (cm, Unreal axes) through the same placement as the bodies (radius 0)
    FVector ComputePointRenderLocationCm(const FVector3d& universeM) const;

    // Returns the index of the body whose surface is nearest the observer, and its altitude in meters
    int32 FindNearestBody(double& outAltitudeM) const;

    // Fired every frame after the clock and bodies advance and before the anchor update; carries the REAL delta time,
    // clamped to SOL::MAX_FRAME_DELTA_S exactly as the clock's was. The ship step runs here so the observer it moves is
    // re-anchored and rebased in the same frame.
    FSOLOnBodiesUpdated& OnBodiesUpdated() { return mOnBodiesUpdated; }

    // Fired after every universe update, once body positions and the render origin are final for the frame
    FSOLOnUniverseUpdated& OnUniverseUpdated() { return mOnUniverseUpdated; }

    // Fired when the render origin moves; the argument is the shift applied to Unreal locations (cm)
    FSOLOnRenderOriginShifted& OnRenderOriginShifted() { return mOnRenderOriginShifted; }

    // Creates the subsystem only in game and PIE worlds that run ASOLGameMode
    virtual bool ShouldCreateSubsystem(UObject* outer) const override;

protected:

    // Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
    virtual bool DoesSupportWorldType(const EWorldType::Type worldType) const override;

private:

    // Snaps the render origin onto the observer when forced or due, then shifts the observer actor and notifies
    void RebaseRenderOrigin(bool bForceSnap);

    UPROPERTY(Transient)
    TObjectPtr<USOLSimClockSubsystem> SimClock;

    UPROPERTY(Transient)
    TObjectPtr<USOLBodyRegistrySubsystem> BodyRegistry;

    TWeakObjectPtr<AActor> mObserverActor;                     // Actor moved when the render origin shifts
    FSOLAnchorSelector mAnchor;                                // Nearest-body selection with hysteresis
    FVector3d mObserverPositionM = FVector3d::ZeroVector;      // Authoritative observer position (universe, meters)
    FSOLRenderOrigin mRenderOrigin;                            // Universe point at Unreal (0,0,0) and its math
    FSOLOnBodiesUpdated mOnBodiesUpdated;                      // Listeners between the body update and the anchor
    FSOLOnUniverseUpdated mOnUniverseUpdated;                  // Listeners for the per-frame update
    FSOLOnRenderOriginShifted mOnRenderOriginShifted;          // Listeners for render-origin shifts
};
