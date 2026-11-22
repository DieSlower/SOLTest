/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Visuals/SOLThrusterRibbonComponent.h"

#include "OrbitLines/SOLOrbitLineMesh.h"
#include "SOLConstants.h"
#include "Visuals/SOLThrusterMath.h"
#include "Visuals/SOLThrusterMirror.h"

#include "Async/ParallelFor.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderingThread.h"
#include "StaticMeshResources.h"

namespace
{
    // Material parameter names set every frame (hashed once)
    const FName RIBBON_FRAME_PARAM(SOL::ThrusterMaterialParams::RIBBON_FRAME);
    const FName RIBBON_TEXTURE_PARAM(SOL::ThrusterMaterialParams::RIBBON_TEXTURE);

    // Texels per texture row: one block of trails
    constexpr int32 RIBBON_ROW_TEXELS = SOL::Thruster::RIBBON_TEXELS_PER_TRAIL * SOL::Thruster::RIBBON_TRAILS_PER_BLOCK;

    // Trails per ParallelFor batch (a trail's points and texels are about a microsecond): a typical spawn view's 20-30
    // pool flames run inline; a batch of 6 was measured slower there (task wake-up and waits, 51e3)
    constexpr int32 RIBBON_FILL_BATCH = 32;

    //////////////////////////////////////////////////////////////////////////
    // Appends one block-mesh vertex: position zero, a fixed basis, the point and side in UV0, the trail in UV1
    void RibbonAddVertex(TArray<FStaticMeshBuildVertex>& vertices, const int32 point, const float around,
        const int32 trail)
    {
        FStaticMeshBuildVertex& vertex = vertices.AddZeroed_GetRef();
        vertex.TangentX = FVector3f::UnitX();
        vertex.TangentY = FVector3f::UnitY();
        vertex.TangentZ = FVector3f::UnitZ();
        vertex.UVs[0] = FVector2f(static_cast<float>(point), around);
        vertex.UVs[1] = FVector2f(static_cast<float>(trail), 0.0f);
        vertex.Color = FColor::White;
    }

    //////////////////////////////////////////////////////////////////////////
    // Creates a float RGBA texture (nearest, linear, never streamed)
    UTexture2D* RibbonCreateTexture(const int32 width, const int32 height)
    {
        UTexture2D* texture = UTexture2D::CreateTransient(width, height, PF_A32B32G32R32F);
        texture->Filter = TF_Nearest;
        texture->SRGB = false;
        texture->CompressionSettings = TC_HDR;
        texture->AddressX = TA_Clamp;
        texture->AddressY = TA_Clamp;
        texture->NeverStream = true;
        texture->UpdateResource();
        return texture;
    }
}

//////////////////////////////////////////////////////////////////////////
// Never ticks, no collision, no shadow, movable at the render-space origin
USOLThrusterRibbonComponent::USOLThrusterRibbonComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetCanEverAffectNavigation(false);
    CastShadow = false;
    Mobility = EComponentMobility::Movable;
    NumCustomDataFloats = 1;
    bUseAsOccluder = false;
}

//////////////////////////////////////////////////////////////////////////
// Builds the block mesh, the texture and the material instance
bool USOLThrusterRibbonComponent::InitializePool(const int32 trailCapacity)
{
    if (RibbonMaterial != nullptr)
    {
        return true;
    }
    UMaterialInterface* baseMaterial = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::THRUSTER_RIBBON_MATERIAL);
    if (baseMaterial == nullptr)
    {
        return false;
    }
    RibbonMaterial = UMaterialInstanceDynamic::Create(baseMaterial, this);
    const FVector4f ribbon = SOLThrusterMirror::MaterialVectors().Ribbon;
    RibbonMaterial->SetVectorParameterValue(FName(SOL::ThrusterMaterialParams::RIBBON),
        FLinearColor(ribbon.X, ribbon.Y, ribbon.Z, ribbon.W));
    mLastFrame = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
    RibbonMaterial->SetVectorParameterValue(RIBBON_FRAME_PARAM, mLastFrame);
    BuildBlockMesh();
    SetStaticMesh(BlockMesh);
    SetMaterial(0, RibbonMaterial);
    SetVisibility(false);
    EnsureRows(FMath::Max(trailCapacity, 1));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Builds the block mesh once: RIBBON_TRAILS_PER_BLOCK tubes of FLAME_DRAWN_POINTS rings (the spine and its two end
// caps), each ring FLAME_TUBE_SIDES + 1 vertices (V = k / sides, the last one repeating the first's position, as
// Niagara's tube slice), two triangles per side per segment (SDD 51 R10-Q2; a cap segment closes the tube in a flat
// cone)
void USOLThrusterRibbonComponent::BuildBlockMesh()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLThrusterRibbonComponent::BuildBlockMesh);
    constexpr int32 points = SOL::Thruster::FLAME_DRAWN_POINTS;
    constexpr int32 trails = SOL::Thruster::RIBBON_TRAILS_PER_BLOCK;
    constexpr int32 sides = SOL::Thruster::FLAME_TUBE_SIDES;
    constexpr int32 ring = sides + 1;
    TArray<FStaticMeshBuildVertex> vertices;
    TArray<uint32> indices;
    vertices.Reserve(trails * points * ring);
    indices.Reserve(trails * (points - 1) * sides * 6);
    for (int32 trail = 0; trail < trails; ++trail)
    {
        const uint32 base = static_cast<uint32>(vertices.Num());
        for (int32 point = 0; point < points; ++point)
        {
            for (int32 k = 0; k < ring; ++k)
            {
                RibbonAddVertex(vertices, point, static_cast<float>(k) / static_cast<float>(sides), trail);
            }
        }

        // Quads between ring j and ring j + 1, side k to k + 1
        for (int32 segment = 0; segment + 1 < points; ++segment)
        {
            for (int32 k = 0; k < sides; ++k)
            {
                const uint32 a = base + static_cast<uint32>(segment * ring + k);
                const uint32 b = a + static_cast<uint32>(ring);
                indices.Append({ a, a + 1, b, b, a + 1, b + 1 });
            }
        }
    }
    mBlockMeshVertexCount = vertices.Num();

    // Every vertex is placed by the material anywhere in render space, so the bounds (also each instance's culling
    // bounds) cover all of it; never ray traced
    BlockMesh = NewObject<UStaticMesh>(this, NAME_None, RF_Transient);
    BlockMesh->SetStaticMaterials({ FStaticMaterial(RibbonMaterial) });
    BlockMesh->bSupportRayTracing = false;
    BlockMesh->SetPositiveBoundsExtension(FVector(SOL::NPC_RENDER_SPACE_BOUNDS_CM));
    BlockMesh->SetNegativeBoundsExtension(FVector(SOL::NPC_RENDER_SPACE_BOUNDS_CM));
    SOLOrbitLineMesh::ApplyRibbonToMesh(*BlockMesh, vertices, indices);
}

//////////////////////////////////////////////////////////////////////////
// Grows the texture rows and ring buffers to hold trailCount trails
void USOLThrusterRibbonComponent::EnsureRows(const int32 trailCount)
{
    const int32 blocks = SOLThruster::RibbonBlockCount(trailCount);
    if (blocks > mRowCapacity)
    {
        // A pending upload may still read the old buffers: flush once (a rare growth, never per frame)
        TRACE_CPUPROFILER_EVENT_SCOPE(USOLThrusterRibbonComponent::GrowRows);
        FlushRenderingCommands();
        int32 rows = FMath::Max(mRowCapacity, 1);
        while (rows < blocks)
        {
            rows *= 2;
        }
        for (TArray<FVector4f>& texels : mTexels)
        {
            texels.Init(FVector4f(0.0f, 0.0f, 0.0f, 0.0f), rows * RIBBON_ROW_TEXELS);
        }
        RibbonTexture = RibbonCreateTexture(RIBBON_ROW_TEXELS, rows);
        RibbonMaterial->SetTextureParameterValue(RIBBON_TEXTURE_PARAM, RibbonTexture);
        mRowCapacity = rows;
    }
}

//////////////////////////////////////////////////////////////////////////
// Fits the instances to the needed blocks: grows at once, removes the trailing blocks after the hysteresis run
void USOLThrusterRibbonComponent::UpdateBlocks(const int32 neededBlocks)
{
    const int32 needed = FMath::Max(neededBlocks, 0);
    if (needed > mBlockCount)
    {
        // Added at the tail, each carrying its first trail (a trail past the live count collapses in the vertex shader)
        TArray<FTransform> identity;
        identity.Init(FTransform::Identity, needed - mBlockCount);
        AddInstances(identity, false, false, false);
        for (int32 block = mBlockCount; block < needed; ++block)
        {
            SetCustomDataValue(block, 0, static_cast<float>(block * SOL::Thruster::RIBBON_TRAILS_PER_BLOCK), false);
        }
        MarkRenderStateDirty();
        mBlockCount = needed;
        mShrinkFrames = 0;
        return;
    }
    if (needed == mBlockCount)
    {
        mShrinkFrames = 0;
        return;
    }

    // Fewer needed: once the need has stayed lower long enough, remove the tail down to the run's largest need, so the
    // remaining blocks keep their index and first-trail custom data
    mShrinkPeakBlocks = mShrinkFrames == 0 ? needed : FMath::Max(mShrinkPeakBlocks, needed);
    if (++mShrinkFrames < SOL::Thruster::RIBBON_BLOCK_SHRINK_FRAMES)
    {
        return;
    }
    TArray<int32> tail;
    tail.Reserve(mBlockCount - mShrinkPeakBlocks);
    for (int32 block = mBlockCount - 1; block >= mShrinkPeakBlocks; --block)
    {
        tail.Add(block);
    }
    RemoveInstances(tail, true);
    MarkRenderStateDirty();
    mBlockCount = mShrinkPeakBlocks;
    mShrinkFrames = 0;
}

//////////////////////////////////////////////////////////////////////////
// Draws this frame's trails: capacity, the parallel texel fill, the live-row upload and the frame
void USOLThrusterRibbonComponent::DrawTrails(const int32 trailCount, const FVector3d& anchorCm,
    TFunctionRef<void(int32 trail, TArrayView<FVector4f> texels)> fill)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLThrusterRibbonComponent::DrawTrails);
    if (RibbonMaterial == nullptr)
    {
        return;
    }
    const bool bShown = trailCount > 0;
    if (bShown != mIsShown)
    {
        SetVisibility(bShown);
        mIsShown = bShown;
    }
    mDrawnTrails = FMath::Max(trailCount, 0);
    mLastFillMs = 0.0;
    UpdateBlocks(SOLThruster::RibbonBlockCount(trailCount));
    if (!bShown)
    {
        return;
    }
    EnsureRows(trailCount);

    // The texels of every live trail into the next ring buffer
    const uint64 fillCycles = FPlatformTime::Cycles64();
    mUploadRing = (mUploadRing + 1) % UE_ARRAY_COUNT(mTexels);
    FVector4f* texels = mTexels[mUploadRing].GetData();
    ParallelFor(TEXT("SOLThrusterRibbonFill"), trailCount, RIBBON_FILL_BATCH, [texels, &fill](const int32 trail)
    {
        const FIntPoint origin = SOLThruster::RibbonTexelOrigin(trail);
        fill(trail, TArrayView<FVector4f>(texels + origin.Y * RIBBON_ROW_TEXELS + origin.X,
            SOL::Thruster::RIBBON_TEXELS_PER_TRAIL));
    });
    mLastFillMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - fillCycles);

    // Only the live rows go up; the shader skips the trails past the count in the last row
    const int32 rows = SOLThruster::RibbonBlockCount(trailCount);
    mRegions[mUploadRing] = FUpdateTextureRegion2D(0, 0, 0, 0, RIBBON_ROW_TEXELS, rows);
    RibbonTexture->UpdateTextureRegions(0, 1, &mRegions[mUploadRing], RIBBON_ROW_TEXELS * sizeof(FVector4f),
        sizeof(FVector4f), reinterpret_cast<uint8*>(texels));

    // The frame (anchor, count) changes only on a re-anchor or a new flame count, so it is sent only then
    const FLinearColor frame(static_cast<float>(anchorCm.X), static_cast<float>(anchorCm.Y),
        static_cast<float>(anchorCm.Z), static_cast<float>(trailCount));
    if (frame != mLastFrame)
    {
        RibbonMaterial->SetVectorParameterValue(RIBBON_FRAME_PARAM, frame);
        mLastFrame = frame;
    }
}
