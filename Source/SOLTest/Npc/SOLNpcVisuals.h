/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Npc/SOLNpcTierProcessor.h"

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"

#include "SOLNpcVisuals.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture2D;
class USOLAnchorSubsystem;
class USOLNpcSubsystem;

/**
 * Presentation of the NPC ships (SDD 11 section 3.4, Part 10c). It does not tick; it redraws on USOLNpcSubsystem's
 * OnNpcsUpdated.
 *
 * - Far: every Far ship is a GPU point. The rails are uploaded once per population into a float texture (two texels
 *   per ship, FSOLNpcRail), re-based to a recent epoch when the rail time has run far from the last one (so the
 *   shader's float time and phases stay small, SDD 11 Amendment 7), and a block mesh of NPC_POINT_BLOCK_SHIPS
 *   triangles is drawn once per block (one ISM instance each, the block's first ship index in its custom data).
 *   M_SOLNpcPoints's vertex shader evaluates each rail from the group points (a tiny per-frame texture, relative to the
 *   viewpoint), applies the far-depth compression of SOLRender::ComputePlacement, hides ships inside the Mid radius
 *   (drawn from the CPU instead), keeps a minimum pixel size and fades brightness with distance. Per frame only the
 *   group texture and a few parameters change, so 1M ships cost no per-frame CPU work or upload.
 * - CPU points: Mid and Full ships beyond the instanced radius but inside the Mid radius (beyond it the far layer
 *   already draws their rail point), positions written each frame into a float texture and drawn by the same material
 *   in CPU mode.
 * - Instanced: one ISM per faction placeholder hull, with per-instance previous transforms (motion vectors, issue
 *   #15) and a minimum on-screen size. Only the instances used this frame or last are updated; the instance count
 *   shrinks again when it stays far above the use.
 * - Mesh: a small pool of static mesh components for the closest ships; a slot that changes ship resets its scene
 *   velocity, so it has no motion vector from the previous owner.
 */
UCLASS()
class SOLTEST_API ASOLNpcVisuals : public AActor
{
    GENERATED_BODY()

public:

    // Creates the root and the instanced components
    ASOLNpcVisuals();

protected:

    // Loads the assets, builds the block mesh and subscribes to NPC updates
    virtual void BeginPlay() override;

    // Unsubscribes from NPC updates
    virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

private:

    // Redraws every tier for this frame; bound to OnNpcsUpdated
    void Redraw();

    // Uploads the population's rails, re-based to the current rail time, and sizes the far blocks (once per population)
    void UploadPopulation();

    // Re-uploads the rails re-based to the current rail time, in place (rail time far from the GPU epoch)
    void ReepochRails();

    // Fills an upload buffer with the population's rails re-based to epochRailTimeS (two texels per ship)
    void FillRailTexels(FVector4f* texels, int32 texelCount, double epochRailTimeS) const;

    // Updates one faction's hull instances: grows or shrinks the count and writes only the used range
    void UploadHulls(int32 faction);

    // Writes this frame's group points (relative to the viewpoint) and the point parameters
    void UpdateFarPoints();

    // Places the CPU ships: mesh pool, instanced hulls and CPU points
    void UpdateCpuShips();

    // Builds the point block mesh (NPC_POINT_BLOCK_SHIPS triangles; vertex z = ship index in the block)
    UStaticMesh* BuildPointBlockMesh();

    // Creates a float RGBA texture of the given size (nearest filtering, linear)
    UTexture2D* CreateFloatTexture(int32 width, int32 height) const;

    // Sizes a points ISM to blockCount instances, the custom data of each being its first ship index
    static void SizePointBlocks(UInstancedStaticMeshComponent* component, int32 blockCount, int32& inOutCurrent);

    // Returns the world size of one pixel at a render distance (cm), from the player camera and the viewport size
    double PixelWorldCm(double distanceCm) const;

    UPROPERTY(VisibleAnywhere, Category = "SOL|Npc")
    TObjectPtr<UInstancedStaticMeshComponent> FarPoints;

    UPROPERTY(VisibleAnywhere, Category = "SOL|Npc")
    TObjectPtr<UInstancedStaticMeshComponent> CpuPoints;

    UPROPERTY(VisibleAnywhere, Category = "SOL|Npc")
    TObjectPtr<UInstancedStaticMeshComponent> HullsFaction0;

    UPROPERTY(VisibleAnywhere, Category = "SOL|Npc")
    TObjectPtr<UInstancedStaticMeshComponent> HullsFaction1;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> MeshPool;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMesh> PointBlockMesh;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> FarMaterial;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> CpuPointMaterial;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> RailTexture;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> GroupTexture;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> CpuTexture;

    UPROPERTY(Transient)
    TObjectPtr<USOLNpcSubsystem> Npcs;

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> Anchor;

    FDelegateHandle mNpcsUpdatedHandle;
    uint32 mUploadedRevision = MAX_uint32;            // Population revision of the rail texture
    int32 mFarBlockCount = 0;
    int32 mCpuBlockCount = 0;
    int32 mCpuTextureRows = 0;
    int32 mHullInstanceCounts[2] = {};
    int32 mHullUsed[2] = {};                          // Instances shown last frame (the range to rewrite)
    int32 mHullLowFrames[2] = {};                     // Frames in a row with the use far below the instance count
    TArray<FVector4f> mGroupTexels[3];                // Ring of group-texture upload buffers (render thread lag)
    TArray<FVector4f> mCpuTexels[3];                  // Ring of CPU-point upload buffers
    FUpdateTextureRegion2D mGroupRegions[3];          // Upload regions, one per ring slot (no per-frame allocation)
    FUpdateTextureRegion2D mCpuRegions[3];
    int32 mUploadRing = 0;
    double mGpuRailEpochS = 0.0;                      // Rail time the GPU copy of the rails is based at
    double mGpuRailEpochRealS = 0.0;                  // Platform time of the last re-base
    TArray<FTransform> mHullTransforms[2];            // Scratch per faction, reused every frame
    TArray<FTransform> mHullPrevTransforms[2];
    TArray<int32> mShipSlots;                         // Scratch: each CPU ship's slot in its layer this frame
    TArray<int32> mMeshPoolOwners;                    // NPC index each pool mesh showed last frame (INDEX_NONE: none)
};
