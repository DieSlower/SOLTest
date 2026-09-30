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
// Render-origin shift (cm); also fired, not meaningfully, on a viewpoint-source change (see OnRenderOriginShifted)
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
 *
 * The render VIEWPOINT is the universe point the frame is drawn from: normally the observer, but a mode with its own
 * camera far from the ship (the jump map, SDD 3) overrides it. The render origin rebases onto the viewpoint, and body
 * placement (1:1 near, depth-compressed far, angular size kept) is computed from it, so the image is correct from that
 * camera. The observer itself (anchor selection, the ship) is unaffected by the override.
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

    // Draws the universe from this point instead of the observer (meters, ecliptic); the render origin follows it
    void SetViewpointOverrideM(const FVector3d& viewpointM);

    // Returns the render viewpoint to the observer; the next update snaps the render origin back onto it
    void ClearViewpointOverride();

    // Returns true while a viewpoint override is active
    bool HasViewpointOverride() const { return mHasViewpointOverride; }

    // Returns the universe point the frame is rendered from: the override if set, else the observer (meters)
    const FVector3d& GetViewpointM() const { return mHasViewpointOverride ? mViewpointOverrideM : mObserverPositionM; }

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

    // Returns a body's render location and radius (cm, Unreal axes); 1:1 near the viewpoint, angular size kept far away
    FSOLRenderPlacement ComputeBodyRenderPlacement(int32 bodyIndex) const;

    // Returns a universe point's render location (cm, Unreal axes) through the same placement as the bodies (radius 0);
    // equals UniverseToRenderCm within 1,000,000 km of the viewpoint
    FVector ComputePointRenderLocationCm(const FVector3d& universeM) const;

    // Returns the index of the body whose surface is nearest the observer, and its altitude in meters
    int32 FindNearestBody(double& outAltitudeM) const;

    // Fired every frame after the clock and bodies advance and before the anchor update; carries the REAL delta time,
    // clamped to SOL::MAX_FRAME_DELTA_S exactly as the clock's was. The ship step runs here so the observer it moves is
    // re-anchored and rebased in the same frame.
    FSOLOnBodiesUpdated& OnBodiesUpdated() { return mOnBodiesUpdated; }

    // Fired after every universe update, once body positions and the render origin are final for the frame
    FSOLOnUniverseUpdated& OnUniverseUpdated() { return mOnUniverseUpdated; }

    // Fired when the render origin moves; the argument is the shift applied to Unreal locations (cm). Caveat: the snap
    // on a viewpoint-source change (the map opening or closing, SOLViewpoint::Resolve's bForceSnap) also fires this
    // with a huge shift between two unrelated viewpoints; it is not a real spatial rebase, so a consumer that fixes up
    // positions (e.g. a future pooled-effect fixup) must not treat it as one (check HasViewpointOverride or re-place)
    FSOLOnRenderOriginShifted& OnRenderOriginShifted() { return mOnRenderOriginShifted; }

    // Creates the subsystem only in game and PIE worlds that run ASOLGameMode
    virtual bool ShouldCreateSubsystem(UObject* outer) const override;

protected:

    // Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
    virtual bool DoesSupportWorldType(const EWorldType::Type worldType) const override;

private:

    // Snaps the render origin onto the viewpoint when forced or due, then places the observer actor and notifies
    void RebaseRenderOrigin(const FVector3d& viewpointM, bool bForceSnap);

    UPROPERTY(Transient)
    TObjectPtr<USOLSimClockSubsystem> SimClock;

    UPROPERTY(Transient)
    TObjectPtr<USOLBodyRegistrySubsystem> BodyRegistry;

    TWeakObjectPtr<AActor> mObserverActor;                     // Actor moved when the render origin shifts
    FSOLAnchorSelector mAnchor;                                // Nearest-body selection with hysteresis
    FVector3d mObserverPositionM = FVector3d::ZeroVector;      // Authoritative observer position (universe, meters)
    FVector3d mViewpointOverrideM = FVector3d::ZeroVector;     // Render viewpoint while overridden (universe, meters)
    bool mHasViewpointOverride = false;                        // True while the viewpoint is not the observer
    bool mWasViewpointOverrideActive = false;                  // Override state at the last update (forces a snap)
    FSOLRenderOrigin mRenderOrigin;                            // Universe point at Unreal (0,0,0) and its math
    FSOLOnBodiesUpdated mOnBodiesUpdated;                      // Listeners between the body update and the anchor
    FSOLOnUniverseUpdated mOnUniverseUpdated;                  // Listeners for the per-frame update
    FSOLOnRenderOriginShifted mOnRenderOriginShifted;          // Listeners for render-origin shifts
};
