/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "MinorBodies/SOLPlanetRing.h"
#include "Universe/SOLRenderOrigin.h"

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MassEntityQuery.h"

#include "SOLRingVisuals.generated.h"

class UInstancedStaticMeshComponent;
class UNiagaraComponent;
class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLSimClockSubsystem;

/**
 * Presentation proxy for one ringed planet's near/far ring rendering (SDD 6 Appendix D, Amendment 11): one
 * UInstancedStaticMeshComponent for the player-streamed near-field rock pool (USOLRingSubsystem's Mass entities,
 * same bulk-transform-update pattern as ASOLAsteroidBeltVisuals) and one UNiagaraComponent for the always-on
 * far-field annulus (the shared, parameterized /Game/SOL/Rings/NS_SOLRingFar system). One actor per ringed planet
 * (SOLPlanetRing::RealRings()), spawned deferred by ASOLGameMode with PlanetName set before BeginPlay.
 *
 * A third layer, the dense rock layer (SDD 6 Amendments 14-15), is the shared /Game/SOL/Rings/NS_SOLRingDense GPU
 * Niagara system: a carpet of ~1M lit rocks in a wrapped window around the camera, drawn only within a few tens of
 * kilometres of the ring (SOLRingDense::ComputeDenseAlpha) and only at low time warp. It is activated once and then
 * only shown/hidden and paused/resumed, never deactivated (reactivating respawns every rock).
 *
 * Near/far cross-fade: SOLRingPatch::ComputeNearFieldAlpha scales every near-field instance's transform (shrinking
 * rocks toward zero size, not toggling visibility, for a genuinely smooth fade) as the player leaves the ring's
 * activation volume, reaching zero at SOLRingPatch::ActivationMarginM() past the ring's physical edge - the exact
 * same margin USOLRingSubsystem itself uses to decide whether to keep the near-field pool populated at all, so the
 * visual fade can never outlast (or cut off before) the data backing it. The far-field Niagara layer is never
 * faded by this actor - by design it always renders the ring's aggregate dust at every distance (SDD 6 Amendment
 * 11), and the near-field rocks appear as additional, more detailed structure on top of it as the player closes
 * in, rather than the two layers swapping between two different "looks" (which would need a second fade channel on
 * the Niagara material that does not exist yet). Revisit this choice at 5e-iv's screenshot pass if it reads wrong.
 */
UCLASS()
class SOLTEST_API ASOLRingVisuals : public AActor
{
    GENERATED_BODY()

public:

    // Creates the root, the near-field ISM and the far-field Niagara component
    ASOLRingVisuals();

    /** Host planet's registry name (FSOLPlanetRingDef::PlanetName); the spawner must set this before BeginPlay. */
    UPROPERTY(EditAnywhere, Category = "SOL|Rings")
    FName PlanetName;

protected:

    // Resolves the ring definition and planet index, wires the far-field Niagara system's User Parameters, builds
    // the near-field Mass query and instances, and subscribes to universe updates
    virtual void BeginPlay() override;

    // Unsubscribes from universe updates
    virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

private:

    // Snapshots the render origin/viewpoint and this frame's near-field alpha once, re-places the far-field Niagara
    // component, and - unless the near field was already fully faded last frame and still is this frame (a cheap
    // early-out, since that is true for >99% of play) - runs the Mass query to write every one of this ring's
    // active entities' render transforms into the scratch array. Returns true if the near field was (re)computed
    // this frame, i.e. whether the caller should push mScratchTransforms to NearFieldMesh.
    bool FillTransforms();

    // Writes the near-field transforms of one chunk's entities that belong to this ring and are currently active
    void ProcessChunk(FMassExecutionContext& context);

    // Re-places every near-field instance (if FillTransforms says it changed) and the far field, refreshing the
    // cross-fade weight for the current frame
    void HandleUniverseUpdated();

    // Updates the dense rock layer: decides whether it is visible this frame (render viewpoint near the ring at a low
    // time warp), and while it is, places it at the viewpoint, integrates its co-rotation angle and feeds the
    // wrapped-window parameters to the Niagara system, all in the Niagara component's mirrored-Y local axes
    void UpdateDenseLayer();

    // Pushes the far field's optional Sun-direction parameter (ring-local, Unreal-handed axes) from the Sun's and
    // the planet's current positions
    void UpdateFarFieldLighting();

    // Re-places and rescales the far-field Niagara component from this frame's render-origin snapshot: location
    // tracks the planet (log-compressed with distance, same as any other body), rotation is mFarFieldRotation
    // (cached once, Unreal axes), and scale compensates for that same log-compression so the annulus's real radius
    // (baked into the Niagara system in centimeters) shrinks with distance exactly like a rendered body's mesh does
    // rather than staying true-to-life size on screen
    void PlaceFarFieldNiagara();

    // Returns how far worldPositionM is outside this ring's physical [InnerRadiusM, OuterRadiusM) band and
    // SOLRingPatch::HalfThicknessM() slab (0 if inside both), for ComputeNearFieldAlpha's distance input
    double DistanceOutsideRingVolumeM(const FVector3d& worldPositionM) const;

    /** Scene root at the render origin. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Visuals")
    TObjectPtr<USceneComponent> SceneRoot;

    /** Near-field ring-rock pool: one instance per Mass entity in this ring's fixed pool. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Visuals")
    TObjectPtr<UInstancedStaticMeshComponent> NearFieldMesh;

    /** Far-field annulus emitter: the shared NS_SOLRingFar system, parameterized per instance in BeginPlay. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Visuals")
    TObjectPtr<UNiagaraComponent> FarFieldNiagara;

    /** Dense rock layer: the shared NS_SOLRingDense system, a camera-local wrapped window of GPU mesh particles. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Visuals")
    TObjectPtr<UNiagaraComponent> DenseNiagara;

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> AnchorSubsystem;

    UPROPERTY(Transient)
    TObjectPtr<USOLSimClockSubsystem> SimClock;

    UPROPERTY(Transient)
    TObjectPtr<USOLBodyRegistrySubsystem> BodyRegistry;

    FSOLPlanetRingDef mRingDef;                                     // Resolved from PlanetName in BeginPlay
    int32 mPlanetIndex = INDEX_NONE;
    FVector3d mRingNormal = FVector3d::ZAxisVector;                 // Host pole direction, ECLIPTIC axes, cached
                                                                     // once in BeginPlay - for DistanceOutsideRing-
                                                                     // VolumeM only; do NOT feed this into an
                                                                     // Unreal-space rotation (use mFarFieldRotation)
    FQuat4d mRingOrientation = FQuat4d::Identity;    // Host orientation (ring-local -> ecliptic), cached in BeginPlay
    int32 mSunIndex = INDEX_NONE;                    // Registry index of the Sun, for the far field's lighting
    double mPlanetGM = 0.0;                          // Host planet's GM, for the dense layer's co-rotation rate
    double mDenseSpinAngleRad = 0.0;                 // Integrated co-rotation angle of the dense carpet
    double mLastSecondsSinceJ2000 = 0.0;             // Sim time at the previous dense update, for the angle's step
    bool mIsDenseEnabled = false;                    // Whether the dense layer's asset and clock resolved (it is optional)
    bool mIsDenseActivated = false;                  // Whether the dense system has been activated yet (first show)
    bool mIsDenseReleased = false;                   // Whether the dense system was deactivated to free its buffers after a long hide
    double mDenseHiddenSinceSeconds = -1.0;          // World time the layer was hidden at (-1 while shown or already released)
    bool mIsDenseVisible = false;                    // Whether the dense layer is currently shown and running
    FVector3d mRingAxisE1 = FVector3d::XAxisVector;  // Ring-frame X axis in ECLIPTIC axes, cached in BeginPlay
    FVector3d mRingAxisE2 = FVector3d::YAxisVector;  // Ring-frame Y axis in ECLIPTIC axes, cached in BeginPlay
    FVector3d mLastSunDirection = FVector3d::ZeroVector; // Last Sun direction pushed to the far field, to skip no-ops
    FQuat mFarFieldRotation = FQuat::Identity;   // mRingNormal converted to Unreal axes, cached once in BeginPlay

    TSharedPtr<FMassEntityManager> mEntityManager;
    FMassEntityQuery mQuery;                     // Ring rocks: render/state/orbit (ro), tag FSOLRingRockTag (All)
    FMassExecuteFunction mChunkFunction;         // Forwards each chunk to ProcessChunk, built once
    TArray<FTransform> mScratchTransforms;       // Sized once to SOLRingPatch::PoolEntityCountPerRing()
    FDelegateHandle mUniverseUpdatedHandle;      // Subscription to the anchor's per-frame update
    FSOLRenderOrigin mFrameRenderOrigin;         // Render origin snapshot, taken once per frame
    FVector3d mFrameViewpointM = FVector3d::ZeroVector; // Viewpoint snapshot, taken once per frame
    double mFrameNearFieldAlpha = 1.0;           // This frame's near-field cross-fade weight, read by ProcessChunk
    double mLastNearFieldAlpha = 1.0;            // Previous frame's alpha, for FillTransforms's early-out
};
