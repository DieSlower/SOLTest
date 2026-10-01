/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "MinorBodies/SOLMinorBodyFragments.h"

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MassEntityQuery.h"
#include "Universe/SOLRenderOrigin.h"

#include "SOLAsteroidBeltVisuals.generated.h"

class UInstancedStaticMeshComponent;
class USOLAnchorSubsystem;

/**
 * Presentation proxy for the asteroid belt's Mass entities (SDD 6 Appendix B): one UInstancedStaticMeshComponent per
 * rendering variant (SOLMinorBodyVariant: real named asteroids, procedural family fill), sized once at BeginPlay to the
 * entity counts USOLMinorBodySubsystem spawned. It does not tick; on the anchor subsystem's OnUniverseUpdated it snapshots
 * the anchor's render origin and viewpoint once (a single UObject read, not one per entity), then runs its own read-only
 * query over the minor-body entities (in parallel chunks, each entity writing only its own slot,
 * FSOLMinorBodyRenderFragment::InstanceIndex, of its variant's reusable scratch array), converts each position through
 * FSOLRenderOrigin::BodyPlacement exactly like USOLAnchorSubsystem::ComputeRenderPlacement does for a registry body, then
 * pushes each variant with ONE BatchUpdateInstancesTransforms call. The placeholder look is the generic body mesh and
 * material (no new art yet).
 *
 * The ISM components are pinned at the Unreal origin with absolute transforms, so the instance transforms are passed
 * in local space (identical to world space there), which skips a per-instance inverse-component transform.
 */
UCLASS()
class SOLTEST_API ASOLAsteroidBeltVisuals : public AActor
{
    GENERATED_BODY()

public:

    // Creates the root and one instanced mesh component per rendering variant
    ASOLAsteroidBeltVisuals();

protected:

    // Sizes the instances to the spawned entities, places them once and subscribes to universe updates
    virtual void BeginPlay() override;

    // Unsubscribes from universe updates
    virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

private:

    // Snapshots the render origin and viewpoint once, then writes every minor body's render transform for the
    // current frame into its variant's scratch array
    void FillTransforms();

    // Writes the render transforms of one chunk's bodies into their variant's scratch array
    void ProcessChunk(FMassExecutionContext& context);

    // Re-places every asteroid instance for the current frame, one bulk update per variant
    void HandleUniverseUpdated();

    /** Scene root at the render origin. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Visuals")
    TObjectPtr<USceneComponent> SceneRoot;

    /** One instanced sphere component per rendering variant, indexed by SOLMinorBodyVariant. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Visuals")
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> VariantMeshes;

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> AnchorSubsystem;

    TSharedPtr<FMassEntityManager> mEntityManager;                  // The world's default Mass entity manager
    FMassEntityQuery mQuery;                                         // Minor bodies: render (ro), state (ro), appearance
    FMassExecuteFunction mChunkFunction;                            // Built once so a frame does not allocate one
    TArray<FTransform> mScratchTransforms[SOLMinorBodyVariant::COUNT]; // Per variant, sized once, rewritten per frame
    FDelegateHandle mUniverseUpdatedHandle;                         // Subscription to the anchor's per-frame update
    FSOLRenderOrigin mFrameRenderOrigin;                            // Render origin snapshot, taken once per frame
    FVector3d mFrameViewpointM = FVector3d::ZeroVector;             // Viewpoint snapshot, taken once per frame
};
