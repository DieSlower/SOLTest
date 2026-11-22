/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Npc/SOLNpcHullInstances.h"

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "Templates/Function.h"

#include "SOLThrusterRibbonComponent.generated.h"

class UMaterialInstanceDynamic;
class UStaticMesh;

/**
 * The GPU strip pool of the thruster flames (SDD 51 section 11.1 R7, 11.2 R10): every shown NPC flame Niagara does not
 * draw, in one instanced draw of M_SOLThrusterRibbon. A block mesh built once at runtime holds RIBBON_TRAILS_PER_BLOCK
 * tubes of FLAME_DRAWN_POINTS rings (the spine and its two end caps) x (FLAME_TUBE_SIDES + 1) vertices (UV0 = drawn
 * point index and around V, UV1.x = the flame in the block, every vertex at zero); one identity instance per block
 * carries its first trail in custom data 0, and the fixed render-space bounds of USOLNpcHullInstances skip every bounds pass. The trails' points live in
 * a float texture of one block per row (SOLThruster::PackRibbonTexels), written each frame by a ParallelFor into a ring
 * of 3 upload buffers and uploaded for the live rows only. Texture rows only grow (pre-sized by InitializePool); the
 * instances follow the live blocks: added the frame a block is needed, the trailing ones removed once the need has stayed
 * lower for RIBBON_BLOCK_SHRINK_FRAMES frames (hidden frames count as a need of 0). A frame with no trail hides the
 * component. Never ticks: ASOLThrusterRibbons calls DrawTrails once per frame.
 */
UCLASS()
class SOLTEST_API USOLThrusterRibbonComponent : public USOLNpcHullInstances
{
    GENERATED_BODY()

public:

    // Never ticks, no collision, no shadow, movable at the render-space origin
    USOLThrusterRibbonComponent();

    // Builds the block mesh, the texture (rows for at least trailCapacity trails; no instance yet) and the material
    // instance; false without the material (the pool then never draws)
    bool InitializePool(int32 trailCapacity);

    // Draws trailCount trails: grows the rows and fits the blocks (UpdateBlocks), has fill(trail, texels) write each
    // trail's RIBBON_TEXELS_PER_TRAIL texels in a ParallelFor (fill must be thread-safe), uploads the live rows and
    // sets the frame (the anchor in render-space cm, exact in float, and the count); a count of 0 hides the pool
    void DrawTrails(int32 trailCount, const FVector3d& anchorCm,
        TFunctionRef<void(int32 trail, TArrayView<FVector4f> texels)> fill);

    // Returns the trails drawn last frame (verification)
    int32 GetDrawnTrailCount() const { return mDrawnTrails; }

    // Returns the instanced blocks (verification)
    int32 GetBlockCapacity() const { return mBlockCount; }

    // Returns the texels uploaded last (verification)
    TConstArrayView<FVector4f> GetLastTexels() const { return mTexels[mUploadRing]; }

    // Returns the block mesh's vertex count (verification)
    int32 GetBlockMeshVertexCount() const { return mBlockMeshVertexCount; }

    // Returns last frame's texel fill time in ms (verification, the A/B profile; the rest of DrawTrails is the upload)
    double GetLastFillMs() const { return mLastFillMs; }

private:

    // Builds the block mesh once (tubes of zero-position vertices, the point, around V and flame in the UVs)
    void BuildBlockMesh();

    // Grows the texture rows and the ring buffers (doubling, a render flush first) to hold trailCount trails; nothing
    // when they already do
    void EnsureRows(int32 trailCount);

    // Fits the instances to this frame's need of blocks: adds the missing ones at once, removes the trailing ones once
    // the need has stayed below the instanced count for RIBBON_BLOCK_SHRINK_FRAMES frames (down to the largest need of
    // that run)
    void UpdateBlocks(int32 neededBlocks);

    UPROPERTY(Transient)
    TObjectPtr<UStaticMesh> BlockMesh;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> RibbonMaterial;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> RibbonTexture;

    TArray<FVector4f> mTexels[3];                       // Ring of texture upload buffers (render thread lag)
    FUpdateTextureRegion2D mRegions[3];                 // Upload regions, one per ring slot
    int32 mUploadRing = 0;
    int32 mRowCapacity = 0;                             // Texture rows (one block each)
    int32 mBlockCount = 0;                              // Instances (blocks)
    int32 mShrinkFrames = 0;                            // Frames in a row the need has stayed below mBlockCount...
    int32 mShrinkPeakBlocks = 0;                        // ...and the largest need of that run
    int32 mDrawnTrails = 0;
    int32 mBlockMeshVertexCount = 0;
    double mLastFillMs = 0.0;
    FLinearColor mLastFrame = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f); // The frame parameter last sent (anchor, count)
    bool mIsShown = false;
};
