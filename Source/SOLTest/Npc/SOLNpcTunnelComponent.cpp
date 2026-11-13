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

namespace
{
    // Developer A/B switch for GPU measurements (SDD 49 section 4.2); the player's switch is the Lanes overlay row
    TAutoConsoleVariable<int32> CVarSOLNpcTunnel(SOL::NPC_TUNNEL_CVAR, 1,
        TEXT("1: NPC lane tunnels drawn near lanes while the lanes are shown; 0: never drawn, and every lane line and "
            "lane ship point back to its issue #38 crossfade (A/B profiling, SDD 49)"));

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
    TunnelMaterial->SetVectorParameterValue(FName(STYLE0), FLinearColor(
        static_cast<float>(SOL::NPC_TUNNEL_RING_WIDTH_PX), static_cast<float>(SOL::NPC_TUNNEL_END_RING_WIDTH_PX),
        static_cast<float>(SOL::NPC_TUNNEL_DOT_SIZE_PX), static_cast<float>(SOL::NPC_LANE_REFERENCE_HEIGHT_PX)));
    TunnelMaterial->SetVectorParameterValue(FName(STYLE1), FLinearColor(static_cast<float>(SOL::NPC_TUNNEL_EMISSIVE),
        static_cast<float>(SOL::NPC_TUNNEL_CHEVRON_ALPHA), static_cast<float>(SOL::NPC_LANE_GLOW_PAD_PX),
        static_cast<float>(SOL::NPC_TUNNEL_DOT_RADIUS_SCALE)));
    TunnelMaterial->SetVectorParameterValue(FName(SHAPE), FLinearColor(
        static_cast<float>(SOL::NPC_TUNNEL_CHEVRON_RADIUS_SCALE),
        static_cast<float>(SOL::NPC_TUNNEL_CHEVRON_HALF_WIDTH_RADII),
        static_cast<float>(SOL::NPC_TUNNEL_CHEVRON_HALF_LENGTH_RADII),
        static_cast<float>(SOL::NPC_TUNNEL_END_RING_MIN_ALPHA)));
    TunnelMaterial->SetVectorParameterValue(FName(STYLE2), FLinearColor(
        static_cast<float>(SOL::NPC_TUNNEL_CHEVRON_NOTCH_FRACTION),
        static_cast<float>(SOL::NPC_TUNNEL_DETAIL_NEAR_FADE_START_RADII),
        static_cast<float>(SOL::NPC_TUNNEL_DETAIL_NEAR_FADE_FULL_RADII),
        static_cast<float>(SOL::NPC_TUNNEL_DOT_WORLD_DIAMETER_RADII)));
    TunnelMaterial->SetVectorParameterValue(FName(STYLE3), FLinearColor(
        static_cast<float>(SOL::NPC_TUNNEL_SCREEN_FADE_HALF_EXTENTS),
        static_cast<float>(SOL::NPC_TUNNEL_CHEVRON_FADE_START_RADII),
        static_cast<float>(SOL::NPC_TUNNEL_CHEVRON_FADE_END_RADII), 0.0f));
    TunnelMaterial->SetVectorParameterValue(FName(LAYOUT0), FLinearColor(
        static_cast<float>(SOL::NPC_TUNNEL_RING_BASE_SPACING_RADII),
        static_cast<float>(SOL::NPC_TUNNEL_RING_SPACING_PER_DISTANCE),
        static_cast<float>(SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE),
        static_cast<float>(SOL::NPC_TUNNEL_DETAIL_SLOTS_PER_SIDE)));
    TunnelMaterial->SetVectorParameterValue(FName(LAYOUT1), FLinearColor(
        static_cast<float>(SOL::NPC_TUNNEL_END_FADE_SPACINGS),
        static_cast<float>(SOL::NPC_TUNNEL_DEPTH_FADE_START_RADII),
        static_cast<float>(SOL::NPC_TUNNEL_DEPTH_FADE_END_RADII),
        static_cast<float>(SOL::NPC_TUNNEL_DEPTH_FADE_FLOOR)));
    TunnelMaterial->SetVectorParameterValue(FName(LAYOUT2), FLinearColor(
        static_cast<float>(SOL::NPC_TUNNEL_NEAR_FADE_START_RADII),
        static_cast<float>(SOL::NPC_TUNNEL_NEAR_FADE_FULL_RADII),
        static_cast<float>(SOL::NPC_TUNNEL_NEAR_FADE_RADIAL_FULL_RADII),
        static_cast<float>(SOL::NPC_TUNNEL_NEAR_FADE_RADIAL_NONE_RADII)));
    TunnelMaterial->SetVectorParameterValue(FName(LAYOUT3), FLinearColor(
        static_cast<float>(SOL::NPC_TUNNEL_DETAIL_FADE_START_RADII),
        static_cast<float>(SOL::NPC_TUNNEL_DETAIL_FADE_END_RADII), static_cast<float>(SOL::NPC_TUNNEL_DOTS_PER_GAP),
        static_cast<float>(SOL::NPC_TUNNEL_DOT_ROWS)));
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

    // The slot mesh and its instanced component (never culled, hidden until a tunnel draws)
    BuildSlotMesh();
    Instances = NewObject<USOLNpcHullInstances>(GetOwner(), TEXT("NpcTunnelInstances"));
    Instances->SetupAttachment(this);
    Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Instances->SetCanEverAffectNavigation(false);
    Instances->SetCastShadow(false);
    Instances->SetUsingAbsoluteLocation(true);
    Instances->SetUsingAbsoluteRotation(true);
    Instances->SetUsingAbsoluteScale(true);
    Instances->SetMobility(EComponentMobility::Movable);
    Instances->NumCustomDataFloats = 1;
    Instances->TranslucencySortPriority = SOL::NPC_TUNNEL_TRANSLUCENCY_SORT_PRIORITY;
    Instances->SetStaticMesh(SlotMesh);
    Instances->SetMaterial(0, TunnelMaterial);
    Instances->SetVisibility(false);
    Instances->RegisterComponent();
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Builds the slot mesh once: ring ribbons, end rings, dot and chevron quads
void USOLNpcTunnelComponent::BuildSlotMesh()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLNpcTunnelComponent::BuildSlotMesh);
    TArray<FStaticMeshBuildVertex> vertices;
    TArray<uint32> indices;
    vertices.Reserve(SOLNpcTunnel::TunnelSlotMeshVertexCount());

    // Rings by slot (level of detail by slot), the two end rings, then per gap slot its dots and its two chevrons
    for (int32 slot = 0; slot < SOLNpcTunnel::RingSlotCount(); ++slot)
    {
        TunnelAddRing(vertices, indices, TUNNEL_KIND_RING, slot, SOLNpcTunnel::RingSegmentsForSlot(slot));
    }
    for (int32 end = 0; end < 2; ++end)
    {
        TunnelAddRing(vertices, indices, TUNNEL_KIND_END_RING, end, SOL::NPC_TUNNEL_RING_SEGMENTS_NEAREST);
    }
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
            TunnelAddQuad(vertices, indices, TUNNEL_KIND_CHEVRON, slot, FVector2f(static_cast<float>(wall), 0.0f));
        }
    }
    mSlotMeshVertexCount = vertices.Num();

    // Every vertex is placed by the material anywhere in render space, so the bounds (also each instance's culling
    // bounds) cover all of it; never ray traced
    SlotMesh = NewObject<UStaticMesh>(this, NAME_None, RF_Transient);
    SlotMesh->SetStaticMaterials({ FStaticMaterial(TunnelMaterial) });
    SlotMesh->bSupportRayTracing = false;
    SlotMesh->SetPositiveBoundsExtension(FVector(SOL::NPC_RENDER_SPACE_BOUNDS_CM));
    SlotMesh->SetNegativeBoundsExtension(FVector(SOL::NPC_RENDER_SPACE_BOUNDS_CM));
    SOLOrbitLineMesh::ApplyRibbonToMesh(*SlotMesh, vertices, indices);
}

//////////////////////////////////////////////////////////////////////////
// Rewrites the instances to one per lane in mInstanceLanes
void USOLNpcTunnelComponent::WriteInstances()
{
    Instances->ClearInstances();
    mIdentityTransforms.Init(FTransform::Identity, mInstanceLanes.Num());
    Instances->AddInstances(mIdentityTransforms, false, false, false);
    for (int32 index = 0; index < mInstanceLanes.Num(); ++index)
    {
        Instances->SetCustomDataValue(index, 0, static_cast<float>(mInstanceLanes[index]), false);
    }
    Instances->MarkRenderStateDirty();
}

//////////////////////////////////////////////////////////////////////////
// Per redraw: shown rule, lane frames, fades and phases, instances on a change, texels, frame and labels
void USOLNpcTunnelComponent::UpdateTunnels(const FSOLNpcLaneFrame& laneFrame, const FSOLNpcGroupTable& groups,
    const FVector3d& viewpointM, const FVector3d& cameraForward, const bool bLanesShown, const bool bMapOpen,
    const FLinearColor& frameParams, const uint64 frameNumber)
{
    SCOPE_CYCLE_COUNTER(STAT_SOL_NpcTunnel);
    if (TunnelTexture == nullptr)
    {
        return;
    }
    mAreTunnelsShown = TunnelMaterial != nullptr && BodyRegistry != nullptr && SOLNpcTunnel::ResolveTunnelsShown(
        mIsOverride, mIsOverrideShown, bLanesShown, CVarSOLNpcTunnel.GetValueOnGameThread() != 0, bMapOpen);

    // Each lane: its feed, effective fade and ring phase; only a lane with a fade above 0 is drawn
    const int32 laneCount = FMath::Min(laneFrame.Lanes.Num(), static_cast<int32>(LANE_CAPACITY));
    mShownLanes.Reset();
    for (int32 lane = 0; lane < LANE_CAPACITY; ++lane)
    {
        mFades[lane] = 0.0;
        mDistanceRadii[lane] = 0.0;
        if (lane >= laneCount || !mAreTunnelsShown)
        {
            mIsInRange[lane] = false;
            mLabelSigns[lane] = 0;
            continue;
        }
        // A lane out of range (fade 0, not forced) needs only its segment distance: no frame is built for it
        const FSOLNpcLaneEntry& entry = laneFrame.Lanes[lane];
        const double segmentM = SOLNpcTunnel::LaneSegmentDistanceM(entry.A - viewpointM, entry.B - viewpointM);
        if (!mIsForceAllLanes && !(SOLNpcTunnel::TunnelFade(segmentM, entry.RadiusM) > 0.0))
        {
            mIsInRange[lane] = false;
            mLabelSigns[lane] = 0;
            continue;
        }
        const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
        const int32 bodyA = groups.LaneBodyA.IsValidIndex(lane) ? groups.LaneBodyA[lane] : INDEX_NONE;
        const int32 bodyB = groups.LaneBodyB.IsValidIndex(lane) ? groups.LaneBodyB[lane] : INDEX_NONE;
        const bool bBodies = bodyA >= 0 && bodyA < registry.Num() && bodyB >= 0 && bodyB < registry.Num();
        FSOLNpcTunnelLaneData data;
        const bool bTunnel = bBodies && SOLNpcTunnel::BuildTunnelLaneData(lane, entry, viewpointM,
            registry.GetRadiusM(bodyA), registry.GetRadiusM(bodyB), data, mFeet[lane]);
        const double fade = bTunnel ? SOLNpcTunnel::EffectiveTunnelFade(data.Fade, true, mIsForceAllLanes) : 0.0;
        if (!(fade > 0.0))
        {
            mIsInRange[lane] = false;
            mLabelSigns[lane] = 0;
            continue;
        }

        // In range: the phase stays fixed to the lane (advanced by the foot arc's change), the label side held
        const FSOLNpcTunnelFoot& foot = mFeet[lane];
        mPhases[lane] = SOLNpcTunnel::StepRingPhase(mIsInRange[lane], mPhases[lane], mLastFootArcs[lane], foot.ArcM,
            foot.RadialM, data.TunnelRadiusM);
        mLastFootArcs[lane] = foot.ArcM;
        mIsInRange[lane] = true;
        mLabelSigns[lane] = SOLNpcTunnel::LabelAheadSign(FVector3d::DotProduct(cameraForward, data.Axis),
            mLabelSigns[lane]);
        data.Fade = fade;
        data.Phase = mPhases[lane];
        mFades[lane] = fade;
        mDistanceRadii[lane] = entry.RadiusM > 0.0 ? segmentM / entry.RadiusM : 0.0;
        mShownLanes.Add(data);
    }

    // Instances only when the set of lanes in range changes; the component visible while one draws
    const bool bDraw = SOLNpcTunnel::ShouldDrawTunnels(mAreTunnelsShown, TConstArrayView<double>(mFades, laneCount));
    if (SOLNpcTunnel::CollectTunnelLanes(TConstArrayView<double>(mFades, laneCount), mInstanceLanes)
        && Instances != nullptr)
    {
        WriteInstances();
    }
    if (bDraw != mAreTunnelsDrawn && Instances != nullptr)
    {
        Instances->SetVisibility(bDraw);
    }
    mAreTunnelsDrawn = bDraw;

    // The texture while a tunnel shows and once more (all zeros) after the last one goes, so no stale fade remains
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
    if (mShownLanes.Num() == 0)
    {
        if (mHasLabels)
        {
            hudEvents->GetBus().ClearLaneTunnelLabels();
            mHasLabels = false;
        }
        return;
    }

    // The labels: strongest first, dimmed after the first, only those bright enough to place
    int32 selected[SOL::NPC_TUNNEL_LABEL_MAX_COUNT];
    const int32 laneCount = FMath::Min(lanes.Num(), static_cast<int32>(LANE_CAPACITY));
    const int32 selectedCount = SOLNpcTunnel::SelectLabelLanes(TConstArrayView<double>(mFades, laneCount),
        TConstArrayView<double>(mDistanceRadii, laneCount), TArrayView<int32>(selected));
    mLabels.Count = 0;
    for (int32 index = 0; index < selectedCount; ++index)
    {
        const int32 lane = selected[index];
        const FSOLNpcTunnelLaneData* data = mShownLanes.FindByPredicate([lane](const FSOLNpcTunnelLaneData& shown)
        {
            return shown.Lane == lane;
        });
        const double alpha = SOLNpcTunnel::LaneLabelAlpha(mFades[lane], index == 0);
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

    // Every shown tunnel's dot rows (lane order, up to the row cap), so no label sits on a dot; none when no label is
    // placed (the HUD then has nothing to keep clear), and none of a tunnel faded below the label cut (its dots are
    // all but invisible)
    mLabels.RowCount = 0;
    for (const FSOLNpcTunnelLaneData& data : mShownLanes)
    {
        if (mLabels.Count == 0 || !SOLNpcTunnel::IsLabelPlaced(data.Fade))
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
// Shows or hides the tunnels for a verification run, optionally forcing every lane's fade to 1
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
// Returns the instanced component's instance count
int32 USOLNpcTunnelComponent::GetInstanceCount() const
{
    return Instances != nullptr ? Instances->GetInstanceCount() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the instanced component is visible
bool USOLNpcTunnelComponent::IsInstanceComponentVisible() const
{
    return Instances != nullptr && Instances->IsVisible();
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
