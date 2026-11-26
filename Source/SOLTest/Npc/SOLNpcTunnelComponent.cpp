/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcTunnelComponent.h"

#include "HudEvents/SOLHudEventSubsystem.h"
#include "Npc/SOLNpcHullInstances.h"
#include "Npc/SOLNpcRail.h"
#include "OrbitLines/SOLOrbitLineMesh.h"
#include "SOLTest.h"
#include "Universe/SOLBodyRegistrySubsystem.h"

#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "StaticMeshResources.h"
#include "Stats/Stats.h"

// Game-thread cost of the tunnels' per-frame update (stat dumpave in -SOLSmokeNpcTunnelProfile, SDD 49 section 4.8)
DECLARE_STATS_GROUP(TEXT("SOLNpcTunnel"), STATGROUP_SOLNpcTunnel, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("SOL_NpcTunnel"), STAT_SOL_NpcTunnel, STATGROUP_SOLNpcTunnel);

// The tunnel classes and the 24-instance pool (SDD 53 7.1, 7.3): instances per class, the drawn lanes the pool left out
// (a fixed-pool limitation, never the level-of-detail rule) and the slot-mesh vertices drawn (stat SOLNpcTunnel)
DECLARE_DWORD_COUNTER_STAT(TEXT("SOL_NpcTunnelFull"), STAT_SOL_NpcTunnelFull, STATGROUP_SOLNpcTunnel);
DECLARE_DWORD_COUNTER_STAT(TEXT("SOL_NpcTunnelReduced"), STAT_SOL_NpcTunnelReduced, STATGROUP_SOLNpcTunnel);
DECLARE_DWORD_COUNTER_STAT(TEXT("SOL_NpcTunnelPoolOverflow"), STAT_SOL_NpcTunnelPoolOverflow, STATGROUP_SOLNpcTunnel);
DECLARE_DWORD_COUNTER_STAT(TEXT("SOL_NpcTunnelVertices"), STAT_SOL_NpcTunnelVertices, STATGROUP_SOLNpcTunnel);

namespace
{
    // Developer A/B switch for GPU measurements (SDD 49 section 4.2); the player's switch is the Lanes overlay row
    TAutoConsoleVariable<int32> CVarSOLNpcTunnel(SOL::NPC_TUNNEL_CVAR, 1,
        TEXT("1: NPC lane tunnels drawn at every distance while the lanes are shown (SDD 52); 0: never drawn, no wall "
            "labels, the lane ships' points on their issue #38 close-up fade (A/B profiling, SDD 49)"));

    // Slot kinds of the mesh's UV0.x (Shaders/Private/SOLNpcTunnel.ush SOLNT_KIND_*)
    constexpr float TUNNEL_KIND_RING = 0.0f;
    constexpr float TUNNEL_KIND_END_RING = 1.0f;
    constexpr float TUNNEL_KIND_DOT = 2.0f;
    constexpr float TUNNEL_KIND_CHEVRON = 3.0f;

    // Material parameter names set every frame or read back (hashed once)
    const FName TUNNEL_FRAME_PARAM(SOL::NpcTunnelMaterialParams::FRAME);
    const FName TUNNEL_GLOW_SCALE_PARAM(SOL::NpcTunnelMaterialParams::GLOW_SCALE);

    // Quad corners of a dot or chevron (UV1), in the order the two triangles index them
    const FVector2f TUNNEL_QUAD_CORNERS[4] = { FVector2f(-1.0f, -1.0f), FVector2f(1.0f, -1.0f), FVector2f(-1.0f, 1.0f),
        FVector2f(1.0f, 1.0f) };

    //////////////////////////////////////////////////////////////////////////
    // Appends one slot-mesh vertex: position zero, a fixed basis, the slot in UV0-UV2
    void TunnelAddVertex(TArray<FStaticMeshBuildVertex>& vertices, const FVector2f& uv0, const FVector2f& uv1,
        const FVector2f& uv2)
    {
        FStaticMeshBuildVertex& vertex = vertices.AddZeroed_GetRef();
        vertex.TangentX = FVector3f::UnitX();
        vertex.TangentY = FVector3f::UnitY();
        vertex.TangentZ = FVector3f::UnitZ();
        vertex.UVs[0] = uv0;
        vertex.UVs[1] = uv1;
        vertex.UVs[2] = uv2;
        vertex.Color = FColor::White;
    }

    //////////////////////////////////////////////////////////////////////////
    // Appends a ring ribbon of a slot: (segments + 1) samples x 2 sides, two triangles per segment
    void TunnelAddRing(TArray<FStaticMeshBuildVertex>& vertices, TArray<uint32>& indices, const float kind,
        const int32 slot, const int32 segments)
    {
        const uint32 base = static_cast<uint32>(vertices.Num());
        for (int32 sample = 0; sample <= segments; ++sample)
        {
            for (const float side : { -1.0f, 1.0f })
            {
                TunnelAddVertex(vertices, FVector2f(kind, static_cast<float>(slot)),
                    FVector2f(static_cast<float>(sample), side), FVector2f(static_cast<float>(segments), 0.0f));
            }
        }
        for (int32 segment = 0; segment < segments; ++segment)
        {
            const uint32 v0 = base + 2 * segment;
            indices.Append({ v0, v0 + 1, v0 + 2, v0 + 2, v0 + 1, v0 + 3 });
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Appends a dot or chevron quad of a gap slot
    void TunnelAddQuad(TArray<FStaticMeshBuildVertex>& vertices, TArray<uint32>& indices, const float kind,
        const int32 slot, const FVector2f& uv2)
    {
        const uint32 base = static_cast<uint32>(vertices.Num());
        for (const FVector2f& corner : TUNNEL_QUAD_CORNERS)
        {
            TunnelAddVertex(vertices, FVector2f(kind, static_cast<float>(slot)), corner, uv2);
        }
        indices.Append({ base, base + 1, base + 2, base + 2, base + 1, base + 3 });
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a linear colour from an sRGB constant
    FLinearColor TunnelLinear(const FColor& color)
    {
        return FLinearColor(color);
    }
}

//////////////////////////////////////////////////////////////////////////
// Never ticks
USOLNpcTunnelComponent::USOLNpcTunnelComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

//////////////////////////////////////////////////////////////////////////
// Creates the texture, the slot mesh, the instanced component and the material
bool USOLNpcTunnelComponent::InitializeTunnels()
{
    using namespace SOL::NpcTunnelMaterialParams;
    using namespace SOL::Hologram;
    if (TunnelTexture != nullptr)
    {
        return TunnelMaterial != nullptr;
    }

    // Per-lane arrays sized once (no allocation per frame), the texture holding zeros (every fade 0) from the start
    constexpr int32 texelCount = LANE_CAPACITY * 4;
    for (TArray<FVector4f>& texels : mTexels)
    {
        texels.Init(FVector4f(0.0f, 0.0f, 0.0f, 0.0f), texelCount);
    }
    mShownLanes.Reserve(LANE_CAPACITY);
    mInstanceLanes.Reserve(LANE_CAPACITY);
    mIdentityTransforms.Reserve(LANE_CAPACITY);
    TunnelTexture = UTexture2D::CreateTransient(LANE_CAPACITY, 4, PF_A32B32G32R32F);
    TunnelTexture->Filter = TF_Nearest;
    TunnelTexture->SRGB = false;
    TunnelTexture->CompressionSettings = TC_HDR;
    TunnelTexture->AddressX = TA_Clamp;
    TunnelTexture->AddressY = TA_Clamp;
    TunnelTexture->NeverStream = true;
    TunnelTexture->UpdateResource();
    mRegions[mUploadRing] = FUpdateTextureRegion2D(0, 0, 0, 0, LANE_CAPACITY, 4);
    TunnelTexture->UpdateTextureRegions(0, 1, &mRegions[mUploadRing], LANE_CAPACITY * sizeof(FVector4f),
        sizeof(FVector4f), reinterpret_cast<uint8*>(mTexels[mUploadRing].GetData()));
    BodyRegistry = GetWorld() != nullptr ? GetWorld()->GetSubsystem<USOLBodyRegistrySubsystem>() : nullptr;

    UMaterialInterface* baseMaterial = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::NPC_TUNNEL_MATERIAL);
    if (baseMaterial == nullptr)
    {
        return false;
    }

    // The material: the tunables once (the shader keeps no copy of them, SOLNpcTunnel.ush)
    TunnelMaterial = UMaterialInstanceDynamic::Create(baseMaterial, this);
    TunnelMaterial->SetTextureParameterValue(FName(TUNNEL_TEXTURE), TunnelTexture);
    const FSOLNpcTunnelMaterialVectors vectors = SOLNpcTunnel::TunnelMaterialVectors();
    const TPair<const TCHAR*, FVector4f> constantVectors[] = { { STYLE0, vectors.Style0 }, { STYLE1, vectors.Style1 },
        { SHAPE, vectors.Shape }, { STYLE2, vectors.Style2 }, { STYLE3, vectors.Style3 }, { LAYOUT0, vectors.Layout0 },
        { LAYOUT1, vectors.Layout1 }, { LAYOUT2, vectors.Layout2 }, { LAYOUT3, vectors.Layout3 },
        { LAYOUT4, vectors.Layout4 } };
    for (const TPair<const TCHAR*, FVector4f>& vector : constantVectors)
    {
        TunnelMaterial->SetVectorParameterValue(FName(vector.Key), FLinearColor(vector.Value.X, vector.Value.Y,
            vector.Value.Z, vector.Value.W));
    }
    TunnelMaterial->SetVectorParameterValue(FName(GLOW0), FLinearColor(static_cast<float>(HUD_GLOW_CORE_HALF_PX),
        static_cast<float>(HUD_GLOW_CORE_FALLOFF), static_cast<float>(HUD_GLOW_CORE_FALLOFF_MAX),
        static_cast<float>(HUD_GLOW_HALO_WEIGHT)));
    TunnelMaterial->SetVectorParameterValue(FName(GLOW1), FLinearColor(static_cast<float>(HUD_GLOW_HALO_SCALE),
        static_cast<float>(HUD_GLOW_HALO_POWER), HUD_GLOW_SCALE_MAX, 0.0f));
    TunnelMaterial->SetVectorParameterValue(FName(RING_COLOR), TunnelLinear(SOL::NPC_TUNNEL_RING_COLOR));
    TunnelMaterial->SetVectorParameterValue(FName(DOT_OUTBOUND_COLOR),
        TunnelLinear(SOL::NPC_TUNNEL_DOT_OUTBOUND_COLOR));
    TunnelMaterial->SetVectorParameterValue(FName(DOT_INBOUND_COLOR),
        TunnelLinear(SOL::NPC_TUNNEL_DOT_INBOUND_COLOR));
    TunnelMaterial->SetVectorParameterValue(FName(CHEVRON_OUTBOUND_COLOR),
        TunnelLinear(SOL::NPC_TUNNEL_CHEVRON_OUTBOUND_COLOR));
    TunnelMaterial->SetVectorParameterValue(FName(CHEVRON_INBOUND_COLOR),
        TunnelLinear(SOL::NPC_TUNNEL_CHEVRON_INBOUND_COLOR));
    TunnelMaterial->SetScalarParameterValue(FName(GLOW_SCALE), mGlowScale);

    // The Full and Reduced meshes and one instanced component per class (never culled, hidden until a tunnel draws);
    // the per-lane state sized once
    BuildSlotMesh();
    Instances = CreateInstances(TEXT("NpcTunnelInstances"), SlotMesh);
    ReducedInstances = CreateInstances(TEXT("NpcTunnelReducedInstances"), ReducedMesh);
    for (TArray<int32>* lanes : { &mFullLanes, &mReducedLanes, &mNewFullLanes, &mNewReducedLanes, &mPoolLanes })
    {
        lanes->Reserve(LANE_CAPACITY);
    }
    PushQualityScale(1.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Creates one class's instanced component: no collision or shadow, absolute transform, the lane in custom data 0
USOLNpcHullInstances* USOLNpcTunnelComponent::CreateInstances(const TCHAR* name, UStaticMesh* mesh)
{
    USOLNpcHullInstances* instances = NewObject<USOLNpcHullInstances>(GetOwner(), name);
    instances->SetupAttachment(this);
    instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    instances->SetCanEverAffectNavigation(false);
    instances->SetCastShadow(false);
    instances->SetUsingAbsoluteLocation(true);
    instances->SetUsingAbsoluteRotation(true);
    instances->SetUsingAbsoluteScale(true);
    instances->SetMobility(EComponentMobility::Movable);
    instances->NumCustomDataFloats = 1;
    instances->TranslucencySortPriority = SOL::NPC_TUNNEL_TRANSLUCENCY_SORT_PRIORITY;
    instances->SetStaticMesh(mesh);
    instances->SetMaterial(0, TunnelMaterial);
    instances->SetVisibility(false);
    instances->RegisterComponent();
    return instances;
}

//////////////////////////////////////////////////////////////////////////
// Builds the slot meshes once: ring ribbons and end rings (the Reduced mesh), then dot and chevron quads (Full)
void USOLNpcTunnelComponent::BuildSlotMesh()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLNpcTunnelComponent::BuildSlotMesh);
    TArray<FStaticMeshBuildVertex> vertices;
    TArray<uint32> indices;
    vertices.Reserve(SOLNpcTunnel::TunnelSlotMeshVertexCount());

    // Rings by slot (level of detail by slot), the two end rings, then per gap slot its dots and its chevrons on the
    // two walls (UV2 = (wall, chevron index)); the rings and end rings come first, so the Reduced mesh is the prefix
    for (int32 slot = 0; slot < SOLNpcTunnel::RingSlotCount(); ++slot)
    {
        TunnelAddRing(vertices, indices, TUNNEL_KIND_RING, slot, SOLNpcTunnel::RingSegmentsForSlot(slot));
    }
    for (int32 end = 0; end < 2; ++end)
    {
        TunnelAddRing(vertices, indices, TUNNEL_KIND_END_RING, end, SOL::NPC_TUNNEL_RING_SEGMENTS_NEAREST);
    }
    const TArray<FStaticMeshBuildVertex> reducedVertices(vertices.GetData(), vertices.Num());
    const TArray<uint32> reducedIndices(indices.GetData(), indices.Num());
    mReducedMeshVertexCount = reducedVertices.Num();
    for (int32 slot = 0; slot < SOLNpcTunnel::DetailSlotCount(); ++slot)
    {
        for (int32 row = 0; row < SOL::NPC_TUNNEL_DOT_ROWS; ++row)
        {
            for (int32 dot = 0; dot < SOL::NPC_TUNNEL_DOTS_PER_GAP; ++dot)
            {
                TunnelAddQuad(vertices, indices, TUNNEL_KIND_DOT, slot,
                    FVector2f(static_cast<float>(row), static_cast<float>(dot)));
            }
        }
        for (int32 wall = 0; wall < 2; ++wall)
        {
            for (int32 chevron = 0; chevron < SOL::NPC_TUNNEL_CHEVRONS_PER_GAP; ++chevron)
            {
                TunnelAddQuad(vertices, indices, TUNNEL_KIND_CHEVRON, slot,
                    FVector2f(static_cast<float>(wall), static_cast<float>(chevron)));
            }
        }
    }
    mSlotMeshVertexCount = vertices.Num();

    // Every vertex is placed by the material anywhere in render space, so the bounds (also each instance's culling
    // bounds) cover all of it; never ray traced
    auto makeMesh = [this](const TArray<FStaticMeshBuildVertex>& meshVertices, const TArray<uint32>& meshIndices)
    {
        UStaticMesh* mesh = NewObject<UStaticMesh>(this, NAME_None, RF_Transient);
        mesh->SetStaticMaterials({ FStaticMaterial(TunnelMaterial) });
        mesh->bSupportRayTracing = false;
        mesh->SetPositiveBoundsExtension(FVector(SOL::NPC_RENDER_SPACE_BOUNDS_CM));
        mesh->SetNegativeBoundsExtension(FVector(SOL::NPC_RENDER_SPACE_BOUNDS_CM));
        SOLOrbitLineMesh::ApplyRibbonToMesh(*mesh, meshVertices, meshIndices);
        return mesh;
    };
    SlotMesh = makeMesh(vertices, indices);
    ReducedMesh = makeMesh(reducedVertices, reducedIndices);
}

//////////////////////////////////////////////////////////////////////////
// Rewrites a component's instances to one per lane in lanes
void USOLNpcTunnelComponent::WriteInstances(USOLNpcHullInstances& instances, const TConstArrayView<int32> lanes,
    TArray<FTransform>& scratchTransforms)
{
    instances.ClearInstances();
    scratchTransforms.Init(FTransform::Identity, lanes.Num());
    instances.AddInstances(scratchTransforms, false, false, false);
    for (int32 index = 0; index < lanes.Num(); ++index)
    {
        instances.SetCustomDataValue(index, 0, static_cast<float>(lanes[index]), false);
    }
    instances.MarkRenderStateDirty();
}

//////////////////////////////////////////////////////////////////////////
// Steps every column's open fade toward its open state; a column taken by another lane starts at 0, a jump snaps
void USOLNpcTunnelComponent::StepOpenFades(const FSOLNpcGroupTable& groups, const FSOLNpcTunnelView& view)
{
    SOLNpcTunnel::StepColumnOpenFades(groups.LaneBodyA, groups.LaneBodyB, view.ColumnOpen, view.RealDeltaS,
        view.bSnapOpenFades || !mHasOpenFades, TArrayView<double>(mOpenFades), TArrayView<int32>(mFadeBodyA),
        TArrayView<int32>(mFadeBodyB), TArrayView<uint8>(mOpenFadeVisible));
    mHasOpenFades = true;
}

//////////////////////////////////////////////////////////////////////////
// Pushes the QualityPreset scale to the material's Layout4.z when it changed
void USOLNpcTunnelComponent::PushQualityScale(const double qualityScale)
{
    const double scale = FMath::IsFinite(qualityScale) && qualityScale > 1.0 ? qualityScale : 1.0;
    if (TunnelMaterial == nullptr || scale == mPushedQualityScale)
    {
        return;
    }
    mPushedQualityScale = scale;
    const FVector4f layout4 = SOLNpcTunnel::TunnelMaterialVectors().Layout4;
    TunnelMaterial->SetVectorParameterValue(FName(SOL::NpcTunnelMaterialParams::LAYOUT4), FLinearColor(layout4.X,
        layout4.Y, static_cast<float>(scale), layout4.W));
}

//////////////////////////////////////////////////////////////////////////
// Per redraw: shown rule, lane frames, fades and phases, instances on a change, texels, frame and labels
void USOLNpcTunnelComponent::UpdateTunnels(const FSOLNpcLaneFrame& laneFrame, const FSOLNpcGroupTable& groups,
    const FVector3d& viewpointM, const FVector3d& cameraForward, const bool bLanesShown, const bool bMapOpen,
    const FLinearColor& frameParams, const uint64 frameNumber, const FSOLNpcTunnelView& view)
{
    SCOPE_CYCLE_COUNTER(STAT_SOL_NpcTunnel);
    if (TunnelTexture == nullptr)
    {
        return;
    }
    mAreTunnelsShown = TunnelMaterial != nullptr && BodyRegistry != nullptr && SOLNpcTunnel::ResolveTunnelsShown(
        mIsOverride, mIsOverrideShown, bLanesShown, CVarSOLNpcTunnel.GetValueOnGameThread() != 0, bMapOpen);

    // The open fades step whether shown or not (a closing lane's slot is held until its fade is over, SDD 53 4.5)
    StepOpenFades(groups, view);

    // Each lane: its pixel class first, from the segment distance and the tunnel radius alone, so an Off lane builds
    // nothing (SDD 53 7.1); a Reduced or Full lane its feed, draw fade (x the open fade), label strength (Full only,
    // the 9.6 -> 19.2 px band at the fade focal, Q20) and ring phase
    const double labelFocalRefPx = SOLNpcTunnel::FadeFocalRefPx(view.FocalRefPx, view.QualityScale);
    mFadeFocalRefPx = labelFocalRefPx;
    mQualityScale = view.QualityScale;
    const int32 laneCount = FMath::Min(laneFrame.Lanes.Num(), static_cast<int32>(LANE_CAPACITY));
    mShownLanes.Reset();
    for (int32 lane = 0; lane < LANE_CAPACITY; ++lane)
    {
        mFades[lane] = 0.0;
        mDrawFades[lane] = 0.0;
        mDistanceRadii[lane] = 0.0;
        mMaxRadiusPx[lane] = 0.0;
        mPoolDrawn[lane] = 0;
        if (lane >= laneCount || !mAreTunnelsShown || !(mOpenFades[lane] > 0.0))
        {
            mLabelSigns[lane] = 0;
            mClasses[lane] = ESOLTunnelClass::Off;
            continue;
        }
        const FSOLNpcLaneEntry& entry = laneFrame.Lanes[lane];
        const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
        const int32 bodyA = groups.LaneBodyA.IsValidIndex(lane) ? groups.LaneBodyA[lane] : INDEX_NONE;
        const int32 bodyB = groups.LaneBodyB.IsValidIndex(lane) ? groups.LaneBodyB[lane] : INDEX_NONE;
        const bool bBodies = bodyA >= 0 && bodyA < registry.Num() && bodyB >= 0 && bodyB < registry.Num();
        if (bodyA != mLabelBodyA[lane] || bodyB != mLabelBodyB[lane])
        {
            // A column reused for another lane (53c2b slots) starts without a label side, from Off and out of the pool
            mLabelSigns[lane] = 0;
            mLabelBodyA[lane] = bodyA;
            mLabelBodyB[lane] = bodyB;
            mClasses[lane] = ESOLTunnelClass::Off;
            mInPool[lane] = 0;
        }
        if (!bBodies || !entry.bResolved)
        {
            mLabelSigns[lane] = 0;
            mClasses[lane] = ESOLTunnelClass::Off;
            continue;
        }
        const double segmentM = SOLNpcTunnel::LaneSegmentDistanceM(entry.A - viewpointM, entry.B - viewpointM);
        mMaxRadiusPx[lane] = SOLLaneNetwork::LaneTunnelMaxRadiusPx(segmentM, SOLNpcTunnel::LaneTunnelRadiusM(entry),
            view.FocalRefPx);
        mClasses[lane] = SOLLaneNetwork::StepTunnelClass(mClasses[lane], mMaxRadiusPx[lane], view.QualityScale);
        FSOLNpcTunnelLaneData data;
        if (mClasses[lane] == ESOLTunnelClass::Off || !SOLNpcTunnel::BuildTunnelLaneData(lane, entry, viewpointM,
            registry.GetRadiusM(bodyA), registry.GetRadiusM(bodyB), data, mFeet[lane], labelFocalRefPx))
        {
            mLabelSigns[lane] = 0;
            mClasses[lane] = ESOLTunnelClass::Off;
            continue;
        }

        // The phase straight from the foot arc on the flat lattice (fixed to the lane, SDD 52 D2); the label side held
        // only while the lane has a label strength
        const FSOLNpcTunnelFoot& foot = mFeet[lane];
        const double labelFade = mClasses[lane] == ESOLTunnelClass::Full ? data.Fade : 0.0;
        const double strength = SOLNpcTunnel::EffectiveTunnelFade(labelFade, true, mIsForceAllLanes);
        mLabelSigns[lane] = strength > 0.0 ? SOLNpcTunnel::LabelAheadSign(FVector3d::DotProduct(cameraForward,
            data.Axis), mLabelSigns[lane]) : 0;
        data.Fade = strength;
        data.DrawFade = SOLNpcTunnel::TunnelDrawFade(true) * mOpenFades[lane];
        data.Phase = SOLNpcTunnel::RingLatticePhase(foot.ArcM, data.TunnelRadiusM);
        mFades[lane] = strength;
        mDrawFades[lane] = data.DrawFade;
        mDistanceRadii[lane] = entry.RadiusM > 0.0 ? data.SegmentDistanceM / entry.RadiusM : 0.0;
        mPoolDrawn[lane] = 1;
        mShownLanes.Add(data);
    }

    // The pool (SDD 53 7.3): the built lanes of both classes ranked by their largest on-screen radius, last update's
    // members kept unless a newcomer is NPC_TUNNEL_POOL_KEEP_RATIO times larger (no swapping at near-equal sizes), the
    // smallest beyond NPC_TUNNEL_INSTANCE_POOL left out (no label, no texels); the overflow logged once per change
    const int32 overflow = SOLLaneNetwork::SelectTunnelPool(TConstArrayView<double>(mMaxRadiusPx, LANE_CAPACITY),
        TConstArrayView<uint8>(mPoolDrawn, LANE_CAPACITY), SOL::NPC_TUNNEL_INSTANCE_POOL, mPoolLanes,
        TConstArrayView<uint8>(mInPool, LANE_CAPACITY), SOL::NPC_TUNNEL_POOL_KEEP_RATIO);
    FMemory::Memzero(mInPool, sizeof(mInPool));
    for (const int32 lane : mPoolLanes)
    {
        mInPool[lane] = 1;
    }
    if (overflow > 0)
    {
        for (int32 lane = 0; lane < LANE_CAPACITY; ++lane)
        {
            mFades[lane] = mInPool[lane] != 0 ? mFades[lane] : 0.0;
            mDrawFades[lane] = mInPool[lane] != 0 ? mDrawFades[lane] : 0.0;
        }
        mShownLanes.RemoveAll([this](const FSOLNpcTunnelLaneData& data) { return mInPool[data.Lane] == 0; });
    }
    if (SOLLaneNetwork::StepOverflowReport(mPoolOverflow, overflow))
    {
        UE_CLOG(overflow > 0, LogSOL, Warning, TEXT("NpcTunnel: %d drawn lanes beyond the %d-instance pool have no "
            "tunnel (the smallest on screen; a fixed-pool limitation, SDD 53 7.3)"), overflow,
            SOL::NPC_TUNNEL_INSTANCE_POOL);
        UE_CLOG(overflow == 0, LogSOL, Log, TEXT("NpcTunnel: the instance pool has room again (overflow %d -> 0)"),
            mPoolOverflow);
    }
    mPoolOverflow = overflow;

    // Instances per class only when that class's lane set changes; each component visible while one of its lanes draws
    SOLLaneNetwork::SplitTunnelPoolByClass(TConstArrayView<uint8>(mInPool, LANE_CAPACITY),
        TConstArrayView<ESOLTunnelClass>(mClasses, LANE_CAPACITY), laneCount, mNewFullLanes, mNewReducedLanes);
    mInstanceLanes.Reset();
    for (int32 lane = 0; lane < laneCount; ++lane)
    {
        if (mInPool[lane] != 0)
        {
            mInstanceLanes.Add(lane);
        }
    }

    // Rewrites one class's instances when its lane set changed and returns whether that class draws (shown, has lanes)
    auto syncClass = [this](USOLNpcHullInstances* instances, TArray<int32>& current, const TArray<int32>& next)
    {
        if (SOLLaneNetwork::SyncTunnelClassLanes(current, next) && instances != nullptr)
        {
            WriteInstances(*instances, current, mIdentityTransforms);
        }
        const bool bClassDraw = mAreTunnelsShown && !current.IsEmpty();
        if (instances != nullptr && instances->IsVisible() != bClassDraw)
        {
            instances->SetVisibility(bClassDraw);
        }
        return bClassDraw;
    };
    const bool bFullDraw = syncClass(Instances, mFullLanes, mNewFullLanes);
    const bool bReducedDraw = syncClass(ReducedInstances, mReducedLanes, mNewReducedLanes);
    const bool bDraw = bFullDraw || bReducedDraw;
    mAreTunnelsDrawn = bDraw;
    SET_DWORD_STAT(STAT_SOL_NpcTunnelFull, mFullLanes.Num());
    SET_DWORD_STAT(STAT_SOL_NpcTunnelReduced, mReducedLanes.Num());
    SET_DWORD_STAT(STAT_SOL_NpcTunnelPoolOverflow, mPoolOverflow);
    SET_DWORD_STAT(STAT_SOL_NpcTunnelVertices, mFullLanes.Num() * mSlotMeshVertexCount
        + mReducedLanes.Num() * mReducedMeshVertexCount);
    if (bDraw)
    {
        PushQualityScale(view.QualityScale);
    }

    // The texture every frame a tunnel draws and once more (all zeros) after the last one goes, so no stale fade
    // remains
    if (SOLNpcTunnel::ShouldUploadTunnelTexels(bDraw, mWasUploading))
    {
        mUploadRing = (mUploadRing + 1) % UE_ARRAY_COUNT(mTexels);
        SOLNpcTunnel::WriteTunnelTexels(mShownLanes, mTexels[mUploadRing]);
        mRegions[mUploadRing] = FUpdateTextureRegion2D(0, 0, 0, 0, LANE_CAPACITY, 4);
        TunnelTexture->UpdateTextureRegions(0, 1, &mRegions[mUploadRing], LANE_CAPACITY * sizeof(FVector4f),
            sizeof(FVector4f), reinterpret_cast<uint8*>(mTexels[mUploadRing].GetData()));
    }
    mWasUploading = bDraw;
    if (bDraw)
    {
        TunnelMaterial->SetVectorParameterValue(TUNNEL_FRAME_PARAM, frameParams);
    }
    PublishLabels(laneFrame.Lanes, groups, viewpointM, frameNumber);
}

//////////////////////////////////////////////////////////////////////////
// Fills and publishes the label snapshot: one label per shown tunnel, strongest first, plus the dot rows
void USOLNpcTunnelComponent::PublishLabels(const TConstArrayView<FSOLNpcLaneEntry> lanes,
    const FSOLNpcGroupTable& groups, const FVector3d& viewpointM, const uint64 frameNumber)
{
    USOLHudEventSubsystem* hudEvents = USOLHudEventSubsystem::Get(this);
    if (hudEvents == nullptr)
    {
        return;
    }
    // The labels by their strength (the old 100 -> 50 lane radii band, SDD 52 D7): strongest first, dimmed after the
    // first, only those bright enough to place; none (the snapshot cleared once) while no lane is close enough
    int32 selected[SOL::NPC_TUNNEL_LABEL_MAX_COUNT];
    const int32 laneCount = FMath::Min(lanes.Num(), static_cast<int32>(LANE_CAPACITY));
    const int32 selectedCount = SOLNpcTunnel::SelectLabelLanes(TConstArrayView<double>(mFades, laneCount),
        TConstArrayView<double>(mDistanceRadii, laneCount), TArrayView<int32>(selected));
    if (selectedCount == 0)
    {
        if (mHasLabels)
        {
            hudEvents->GetBus().ClearLaneTunnelLabels();
            mHasLabels = false;
        }
        return;
    }
    mLabels.Count = 0;
    for (int32 index = 0; index < selectedCount; ++index)
    {
        const int32 lane = selected[index];
        const FSOLNpcTunnelLaneData* data = mShownLanes.FindByPredicate([lane](const FSOLNpcTunnelLaneData& shown)
        {
            return shown.Lane == lane;
        });
        // The label fades with its tunnel at a close or reopen (the open fade); row 1 .w keeps the strength alone
        const double alpha = SOLNpcTunnel::LaneLabelAlpha(mFades[lane], index == 0) * mOpenFades[lane];
        if (data == nullptr || !SOLNpcTunnel::IsLabelPlaced(alpha))
        {
            continue;
        }
        FSOLNpcTunnelBasis basis;
        basis.Axis = data->Axis;
        basis.Up = data->Up;
        basis.Right = FVector3d::CrossProduct(data->Axis, data->Up);
        basis.bValid = true;
        const double footArcM = mFeet[lane].ArcM;
        const FVector2D endDistancesM = SOLNpcTunnel::LaneEndDistancesM(viewpointM, lanes[lane], basis,
            FVector2D(data->EndArcAM + footArcM, data->EndArcBM + footArcM));
        FSOLHudLaneTunnelLabel& label = mLabels.Labels[mLabels.Count++];
        label.Lane = lane;
        label.Strength = mFades[lane];
        label.bStrongest = index == 0;
        label.Alpha = alpha;
        label.DistanceAM = endDistancesM.X;
        label.DistanceBM = endDistancesM.Y;
        label.ShipCount = lanes[lane].ShipCount;
        label.BodyA = groups.LaneBodyA.IsValidIndex(lane) ? groups.LaneBodyA[lane] : INDEX_NONE;
        label.BodyB = groups.LaneBodyB.IsValidIndex(lane) ? groups.LaneBodyB[lane] : INDEX_NONE;
        label.TopAnchorM = SOLNpcTunnel::LaneLabelAnchorRelM(data->FootRelM, basis, data->TunnelRadiusM,
            mLabelSigns[lane], 0.0);
        label.BottomAnchorM = SOLNpcTunnel::LaneLabelAnchorRelM(data->FootRelM, basis, data->TunnelRadiusM,
            mLabelSigns[lane], 180.0);
    }

    // The dot rows of every drawn tunnel whose dots can be visible (lane segment within the gap gate's reach, SDD 52
    // D4), in lane order up to the row cap, so no label sits on any visible dot row, whatever that lane's label
    // strength; none when no label is placed (the HUD then has nothing to keep clear)
    mLabels.RowCount = 0;
    for (const FSOLNpcTunnelLaneData& data : mShownLanes)
    {
        if (mLabels.Count == 0 || !SOLNpcTunnel::IsDotRowInReach(data.SegmentDistanceM, data.TunnelRadiusM))
        {
            continue;
        }
        FSOLNpcTunnelBasis basis;
        basis.Axis = data.Axis;
        basis.Up = data.Up;
        basis.Right = FVector3d::CrossProduct(data.Axis, data.Up);
        basis.bValid = true;
        const int32 room = SOL::NPC_TUNNEL_LABEL_MAX_ROWS - mLabels.RowCount;
        if (room <= 0)
        {
            break;
        }
        const int32 written = SOLNpcTunnel::TunnelDotRowSegmentsRelM(data.FootRelM, basis, data.TunnelRadiusM,
            data.EndArcAM, data.EndArcBM, TArrayView<FVector3d>(mLabels.RowStartM + mLabels.RowCount, room),
            TArrayView<FVector3d>(mLabels.RowEndM + mLabels.RowCount, room));
        for (int32 row = mLabels.RowCount; row < mLabels.RowCount + written; ++row)
        {
            mLabels.RowDotDiameterM[row] = SOL::NPC_TUNNEL_DOT_WORLD_DIAMETER_RADII * data.TunnelRadiusM;
        }
        mLabels.RowCount += written;
    }
    hudEvents->GetBus().PublishLaneTunnelLabels(mLabels, frameNumber);
    mHasLabels = true;
}

//////////////////////////////////////////////////////////////////////////
// Pushes the HUD glow scale to the material
void USOLNpcTunnelComponent::SetGlowScale(const float glowScale)
{
    mGlowScale = glowScale;
    if (TunnelMaterial != nullptr)
    {
        TunnelMaterial->SetScalarParameterValue(TUNNEL_GLOW_SCALE_PARAM, glowScale);
    }
}

//////////////////////////////////////////////////////////////////////////
// Shows or hides the tunnels for a verification run, optionally forcing every lane's label strength to 1
void USOLNpcTunnelComponent::SetVerificationOverride(const bool bShown, const bool bForceAllLanes)
{
    mIsOverride = true;
    mIsOverrideShown = bShown;
    mIsForceAllLanes = bForceAllLanes;
}

//////////////////////////////////////////////////////////////////////////
// Returns the material's GlowScale as it holds it
bool USOLNpcTunnelComponent::GetPushedGlowScale(float& outGlowScale) const
{
    return TunnelMaterial != nullptr && TunnelMaterial->GetScalarParameterValue(
        FHashedMaterialParameterInfo(TUNNEL_GLOW_SCALE_PARAM), outGlowScale);
}

//////////////////////////////////////////////////////////////////////////
// Returns the instance count of both classes' instanced components
int32 USOLNpcTunnelComponent::GetInstanceCount() const
{
    return (Instances != nullptr ? Instances->GetInstanceCount() : 0)
        + (ReducedInstances != nullptr ? ReducedInstances->GetInstanceCount() : 0);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether either class's instanced component is visible
bool USOLNpcTunnelComponent::IsInstanceComponentVisible() const
{
    return (Instances != nullptr && Instances->IsVisible())
        || (ReducedInstances != nullptr && ReducedInstances->IsVisible());
}

//////////////////////////////////////////////////////////////////////////
// Returns a lane's pixel class of the last update (Off out of range)
ESOLTunnelClass USOLNpcTunnelComponent::GetTunnelClass(const int32 lane) const
{
    return lane >= 0 && lane < LANE_CAPACITY ? mClasses[lane] : ESOLTunnelClass::Off;
}

//////////////////////////////////////////////////////////////////////////
// Returns a lane's open fade of the last update (0 out of range)
double USOLNpcTunnelComponent::GetOpenFade(const int32 lane) const
{
    return lane >= 0 && lane < LANE_CAPACITY ? mOpenFades[lane] : 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Clears the label snapshot when the component goes away
void USOLNpcTunnelComponent::EndPlay(const EEndPlayReason::Type endPlayReason)
{
    if (mHasLabels)
    {
        if (USOLHudEventSubsystem* hudEvents = USOLHudEventSubsystem::Get(this))
        {
            hudEvents->GetBus().ClearLaneTunnelLabels();
        }
        mHasLabels = false;
    }
    Super::EndPlay(endPlayReason);
}
