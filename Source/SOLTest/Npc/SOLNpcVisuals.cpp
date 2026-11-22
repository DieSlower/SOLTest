/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcVisuals.h"

#include "Menu/SOLSettingsSubsystem.h"
#include "Npc/SOLNpcAiMath.h"
#include "Npc/SOLNpcHullInstances.h"
#include "Npc/SOLNpcRail.h"
#include "Npc/SOLNpcScaleMath.h"
#include "Npc/SOLNpcSubsystem.h"
#include "Npc/SOLNpcTunnelComponent.h"
#include "Npc/SOLNpcTunnelMath.h"
#include "Npc/SOLNpcTrafficMirror.h"
#include "OrbitLines/SOLOrbitLineComponent.h"
#include "OrbitLines/SOLOrbitLineMesh.h"
#include "OrbitLines/SOLOrbitLineVisuals.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Ship/SOLShipPawn.h"
#include "UI/SOLOverlayModel.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLRenderPlacement.h"
#include "Visuals/SOLThrusterRibbons.h"

#include "Async/ParallelFor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "Misc/App.h"
#include "Stats/Stats.h"
#include "StaticMeshAttributes.h"

// Game-thread cost of the NPC redraw and of its per-frame lane share (stat dumpave in -SOLSmokeNpcLanesProfile)
DECLARE_STATS_GROUP(TEXT("SOLNpc"), STATGROUP_SOLNpc, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("SOL_NpcRedraw"), STAT_SOL_NpcRedraw, STATGROUP_SOLNpc);
DECLARE_CYCLE_STAT(TEXT("SOL_NpcLanes"), STAT_SOL_NpcLanes, STATGROUP_SOLNpc);

namespace
{
    // Developer A/B switch for GPU measurements (SDD 38 D20); the player's switch is the Lanes overlay row
    TAutoConsoleVariable<int32> CVarSOLNpcLanes(SOL::NPC_LANE_CVAR, 1,
        TEXT("1: NPC lane lines drawn while the Lanes overlay row is shown; 0: never drawn and every lane ship's far "
            "point drawn as before (A/B profiling, SDD 38)"));

    // Developer A/B switch for the plume cost (SDD 51 part 51c): the plain hull shapes against the thruster meshes
    TAutoConsoleVariable<int32> CVarSOLNpcPlumes(SOL::NPC_PLUME_CVAR, 1,
        TEXT("1: NPC hulls drawn with their thruster plume sections (SDD 51); 0: the plain engine shapes and no plume "
            "floats (A/B profiling)"));

    // Per-ship plume floats per hull instance (SOL::ThrusterMaterialParams)
    constexpr int32 PLUME_FLOATS = SOL::ThrusterMaterialParams::CUSTOM_DATA_FLOATS;

    // Material parameter names of M_SOLNpcPoints and M_SOLNpcHull (Tools/Npc/create_npc_assets.py)
    const FName PARAM_RAIL_TEX(TEXT("NpcRailTex"));
    const FName PARAM_GROUP_TEX(TEXT("NpcGroupTex"));
    const FName PARAM_CPU_TEX(TEXT("NpcCpuTex"));
    const FName PARAM_0(TEXT("NpcParams0"));          // (rail time since the GPU epoch s, Mid radius m, ship count,
                                                      //  mode 0 rail / 1 CPU)
    const FName PARAM_1(TEXT("NpcParams1"));          // (viewpoint render cm xyz, max render distance cm)
    const FName PARAM_2(TEXT("NpcParams2"));          // (min pixels, ship radius m, brightness ref m, brightness min)
    const FName PARAM_3(TEXT("NpcParams3"));          // (brightness exponent, emissive, rail tex width, CPU tex width)
    const FName PARAM_FACTION_0(TEXT("NpcFaction0Color"));
    const FName PARAM_FACTION_1(TEXT("NpcFaction1Color"));
    const FName PARAM_HULL_TINT(TEXT("Tint"));

    // Lane parameters (SOL::NpcLaneMaterialParams; M_SOLNpcLanes and the far points' crossfade)
    const FName PARAM_LANE_0(SOL::NpcLaneMaterialParams::LANE_PARAMS0);
    const FName PARAM_LANE_FRAME(SOL::NpcLaneMaterialParams::FRAME);
    const FName PARAM_POINT_LANES(SOL::NpcLaneMaterialParams::POINT_LANE_PARAMS);
    const FName PARAM_LANE_GLOW_SCALE(SOL::NpcLaneMaterialParams::GLOW_SCALE);
    const FName PARAM_TUNNEL_TEX(SOL::NpcLaneMaterialParams::TUNNEL_TEXTURE);

    // Placeholder hull: point corners of the billboard triangle (incircle radius 1)
    const FVector2f POINT_CORNERS[3] = { FVector2f(0.0f, 2.0f), FVector2f(-1.7320508f, -1.0f),
        FVector2f(1.7320508f, -1.0f) };

    // Scale of the billboard corners in the block mesh's local positions (cm), so triangles are not degenerate
    constexpr float POINT_CORNER_LOCAL_SCALE = 0.01f;

    // Size of the mesh pool for the closest ships
    constexpr int32 NPC_MESH_POOL_SIZE = 256;

    // Basic-shape meshes are 100 cm across
    constexpr double BASIC_SHAPE_SIZE_CM = 100.0;

    //////////////////////////////////////////////////////////////////////////
    // Configures an instanced component as a shadowless, non-colliding presentation layer
    void ConfigurePresentationComponent(UInstancedStaticMeshComponent* component)
    {
        component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        component->SetCanEverAffectNavigation(false);
        component->SetCastShadow(false);
        component->SetUsingAbsoluteLocation(true);
        component->SetUsingAbsoluteRotation(true);
        component->SetUsingAbsoluteScale(true);
        component->SetMobility(EComponentMobility::Movable);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the placeholder hull transform of a ship (faction 0 cone along its heading, faction 1 flat box with its
    // flat face toward the body top)
    FTransform HullTransform(const FVector& locationCm, const FQuat& rotation, const int32 faction,
        const double lengthCm)
    {
        const FVector scale = faction == 0 ? FVector(0.4, 0.4, 1.0) : FVector(1.0, 0.5, 0.18);
        return FTransform(rotation, locationCm, scale * (lengthCm / BASIC_SHAPE_SIZE_CM));
    }
}

//////////////////////////////////////////////////////////////////////////
// Creates the root and the instanced components
ASOLNpcVisuals::ASOLNpcVisuals()
{
    PrimaryActorTick.bCanEverTick = false;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    FarPoints = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("FarPoints"));
    CpuPoints = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("CpuPoints"));
    HullsFaction0 = CreateDefaultSubobject<USOLNpcHullInstances>(TEXT("HullsFaction0"));
    HullsFaction1 = CreateDefaultSubobject<USOLNpcHullInstances>(TEXT("HullsFaction1"));
    for (UInstancedStaticMeshComponent* component : { FarPoints.Get(), CpuPoints.Get(), HullsFaction0.Get(),
        HullsFaction1.Get() })
    {
        component->SetupAttachment(RootComponent);
        ConfigurePresentationComponent(component);
    }
    FarPoints->NumCustomDataFloats = 1;
    CpuPoints->NumCustomDataFloats = 1;
    HullsFaction0->NumCustomDataFloats = PLUME_FLOATS;
    HullsFaction1->NumCustomDataFloats = PLUME_FLOATS;

    // Lane lines (SDD 38): one never-culled ribbon component at the origin, its mesh built per population
    LaneLines = CreateDefaultSubobject<USOLOrbitLineComponent>(TEXT("LaneLines"));
    LaneLines->SetupAttachment(RootComponent);
    LaneLines->SetUsingAbsoluteLocation(true);
    LaneLines->SetUsingAbsoluteRotation(true);
    LaneLines->SetUsingAbsoluteScale(true);
    LaneLines->SetMobility(EComponentMobility::Movable);
    LaneLines->TranslucencySortPriority = SOL::NPC_LANE_TRANSLUCENCY_SORT_PRIORITY;
    LaneLines->SetVisibility(false);

    // Lane tunnels (SDD 49): created here, set up in BeginPlay, updated from UpdateLanes (never ticks)
    Tunnels = CreateDefaultSubobject<USOLNpcTunnelComponent>(TEXT("Tunnels"));
    Tunnels->SetupAttachment(RootComponent);
}

//////////////////////////////////////////////////////////////////////////
// Gives the lanes the orbit-line actor whose occluders they share, and tells it whether lanes draw now
void ASOLNpcVisuals::SetOrbitLineVisuals(ASOLOrbitLineVisuals* orbitLines)
{
    OrbitLines = orbitLines;
    if (IsValid(OrbitLines))
    {
        OrbitLines->SetLanesDrawn(mIsLaneOverlayDrawn);
    }
}

//////////////////////////////////////////////////////////////////////////
// Detaches the lanes from the Lanes row for a verification run and shows or hides them as given
void ASOLNpcVisuals::SetLaneVerificationOverride(const bool bShown)
{
    mIsLaneOverride = true;
    mIsLaneOverrideShown = bShown;
}

//////////////////////////////////////////////////////////////////////////
// Shows or hides the lane tunnels for a verification run, optionally forcing every lane's fade to 1
void ASOLNpcVisuals::SetTunnelVerificationOverride(const bool bShown, const bool bForceAllLanes)
{
    Tunnels->SetVerificationOverride(bShown, bForceAllLanes);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the lane ribbon component is visible
bool ASOLNpcVisuals::IsLaneComponentVisible() const
{
    return LaneLines != nullptr && LaneLines->IsVisible();
}

//////////////////////////////////////////////////////////////////////////
// Returns the lane material's GlowScale and the far points' NpcParams4 as the materials hold them
bool ASOLNpcVisuals::GetPushedLaneParams(float& outGlowScale, FLinearColor& outPointLaneParams) const
{
    if (LaneMaterial == nullptr || FarMaterial == nullptr)
    {
        return false;
    }
    return LaneMaterial->GetScalarParameterValue(FHashedMaterialParameterInfo(PARAM_LANE_GLOW_SCALE), outGlowScale)
        && FarMaterial->GetVectorParameterValue(FHashedMaterialParameterInfo(PARAM_POINT_LANES), outPointLaneParams);
}

//////////////////////////////////////////////////////////////////////////
// Returns the lane material's NpcLaneStyle2 as the material holds it
bool ASOLNpcVisuals::GetPushedLaneStyle2(FLinearColor& outStyle2) const
{
    return LaneMaterial != nullptr && LaneMaterial->GetVectorParameterValue(
        FHashedMaterialParameterInfo(FName(SOL::NpcLaneMaterialParams::STYLE2)), outStyle2);
}

//////////////////////////////////////////////////////////////////////////
// Loads the assets, builds the block mesh and subscribes to NPC updates
void ASOLNpcVisuals::BeginPlay()
{
    Super::BeginPlay();
    Npcs = GetWorld()->GetSubsystem<USOLNpcSubsystem>();
    Anchor = GetWorld()->GetSubsystem<USOLAnchorSubsystem>();
    UMaterialInterface* pointMaterial = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::NPC_POINTS_MATERIAL);
    UMaterialInterface* hullMaterial = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::NPC_HULL_MATERIAL);
    // Hull meshes: the thruster meshes (hull slot 0 plus the plume, glow and burst sections, SDD 51 section 4.10) and
    // the plain engine shapes they were generated from (the sol.NpcPlumes 0 baseline, same size and pivot)
    PlumeHullMeshes[0] = LoadObject<UStaticMesh>(nullptr, SOL::Paths::THRUSTER_NPC_HULL_MESH_FACTION_0);
    PlumeHullMeshes[1] = LoadObject<UStaticMesh>(nullptr, SOL::Paths::THRUSTER_NPC_HULL_MESH_FACTION_1);
    PlainHullMeshes[0] = LoadObject<UStaticMesh>(nullptr, SOL::Paths::NPC_HULL_MESH_FACTION_0);
    PlainHullMeshes[1] = LoadObject<UStaticMesh>(nullptr, SOL::Paths::NPC_HULL_MESH_FACTION_1);
    if (PlumeHullMeshes[0] == nullptr || PlumeHullMeshes[1] == nullptr)
    {
        UE_LOG(LogSOL, Warning, TEXT("NpcVisuals %s: thruster hull meshes missing; NPC plumes not drawn"), *GetName());
        PlumeHullMeshes[0] = PlainHullMeshes[0];
        PlumeHullMeshes[1] = PlainHullMeshes[1];
    }
    mArePlumeMeshes = CVarSOLNpcPlumes.GetValueOnGameThread() != 0;
    UStaticMesh* hullMeshes[2] = {
        mArePlumeMeshes ? PlumeHullMeshes[0].Get() : PlainHullMeshes[0].Get(),
        mArePlumeMeshes ? PlumeHullMeshes[1].Get() : PlainHullMeshes[1].Get(),
    };
    if (Npcs == nullptr || Anchor == nullptr || pointMaterial == nullptr || hullMaterial == nullptr
        || hullMeshes[0] == nullptr || hullMeshes[1] == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("NpcVisuals %s: NPC subsystem, anchor or NPC assets missing; NPCs not drawn"),
            *GetName());
        return;
    }

    // Point layers: one shared block mesh, two material instances (rail mode and CPU mode)
    PointBlockMesh = BuildPointBlockMesh();
    GroupTexture = CreateFloatTexture(SOL::NPC_GROUP_TEXTURE_WIDTH, SOL::NPC_GROUP_TEXTURE_ROWS);
    FarMaterial = UMaterialInstanceDynamic::Create(pointMaterial, this);
    CpuPointMaterial = UMaterialInstanceDynamic::Create(pointMaterial, this);
    for (UMaterialInstanceDynamic* material : { FarMaterial.Get(), CpuPointMaterial.Get() })
    {
        material->SetTextureParameterValue(PARAM_GROUP_TEX, GroupTexture);
        material->SetVectorParameterValue(PARAM_FACTION_0, SOL::NPC_FACTION_COLORS[0]);
        material->SetVectorParameterValue(PARAM_FACTION_1, SOL::NPC_FACTION_COLORS[1]);
    }
    // The tunnels (SDD 49): their texture always exists (zeros), so the lane ships' crossfade reads F = 0 without them
    if (!Tunnels->InitializeTunnels())
    {
        UE_LOG(LogSOL, Warning, TEXT("NpcVisuals %s: %s missing; NPC lane tunnels not drawn"), *GetName(),
            SOL::Paths::NPC_TUNNEL_MATERIAL);
    }
    for (UMaterialInstanceDynamic* material : { FarMaterial.Get(), CpuPointMaterial.Get() })
    {
        material->SetTextureParameterValue(PARAM_TUNNEL_TEX, Tunnels->GetTunnelTexture());
    }
    FarPoints->SetStaticMesh(PointBlockMesh);
    FarPoints->SetMaterial(0, FarMaterial);
    CpuPoints->SetStaticMesh(PointBlockMesh);
    CpuPoints->SetMaterial(0, CpuPointMaterial);

    // Lane lines (SDD 38): the settings are read directly (the Lanes row, the live HUD glow; D23); a missing lane
    // material leaves the lane ships' points exactly as before
    Settings = USOLSettingsSubsystem::Get(this);
    if (!SetUpLanes())
    {
        UE_LOG(LogSOL, Warning, TEXT("NpcVisuals %s: %s missing; NPC lane lines not drawn"), *GetName(),
            SOL::Paths::NPC_LANES_MATERIAL);
    }

    // Hull layers: one ISM per faction, and the closest-ship mesh pool
    UInstancedStaticMeshComponent* hulls[2] = { HullsFaction0, HullsFaction1 };
    for (int32 faction = 0; faction < 2; ++faction)
    {
        UMaterialInstanceDynamic* tint = UMaterialInstanceDynamic::Create(hullMaterial, this);
        tint->SetVectorParameterValue(PARAM_HULL_TINT, SOL::NPC_FACTION_COLORS[faction]);
        hulls[faction]->SetStaticMesh(hullMeshes[faction]);
        hulls[faction]->SetMaterial(0, tint);
        hulls[faction]->SetHasPerInstancePrevTransforms(true);
        hulls[faction]->SetNumCustomDataFloats(mArePlumeMeshes ? PLUME_FLOATS : 0);
    }
    MeshPool.Reserve(NPC_MESH_POOL_SIZE);
    for (int32 slot = 0; slot < NPC_MESH_POOL_SIZE; ++slot)
    {
        UStaticMeshComponent* mesh = NewObject<UStaticMeshComponent>(this);
        mesh->SetupAttachment(RootComponent);
        mesh->SetUsingAbsoluteLocation(true);
        mesh->SetUsingAbsoluteRotation(true);
        mesh->SetUsingAbsoluteScale(true);
        mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        mesh->SetCastShadow(false);
        mesh->SetVisibility(false);
        mesh->RegisterComponent();
        MeshPool.Add(mesh);
    }
    mMeshPoolOwners.Init(INDEX_NONE, NPC_MESH_POOL_SIZE);
    mMeshPoolShips.Reserve(NPC_MESH_POOL_SIZE);
    for (int32 faction = 0; faction < 2; ++faction)
    {
        UMaterialInterface* tint = hulls[faction]->GetMaterial(0);
        for (int32 slot = faction; slot < NPC_MESH_POOL_SIZE; slot += 2)
        {
            MeshPool[slot]->SetStaticMesh(hullMeshes[faction]);
            MeshPool[slot]->SetMaterial(0, tint);
        }
    }

    mNpcsUpdatedHandle = Npcs->OnNpcsUpdated().AddUObject(this, &ASOLNpcVisuals::Redraw);
}

//////////////////////////////////////////////////////////////////////////
// Unsubscribes from NPC updates
void ASOLNpcVisuals::EndPlay(const EEndPlayReason::Type endPlayReason)
{
    if (Npcs != nullptr)
    {
        Npcs->OnNpcsUpdated().Remove(mNpcsUpdatedHandle);
    }
    mNpcsUpdatedHandle.Reset();
    if (IsValid(OrbitLines))
    {
        OrbitLines->SetLanesDrawn(false);
    }
    Super::EndPlay(endPlayReason);
}

//////////////////////////////////////////////////////////////////////////
// Redraws every tier for this frame; bound to OnNpcsUpdated
void ASOLNpcVisuals::Redraw()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLNpcVisuals::Redraw);
    SCOPE_CYCLE_COUNTER(STAT_SOL_NpcRedraw);
    if (FarMaterial == nullptr)
    {
        return;
    }
    mUploadRing = (mUploadRing + 1) % UE_ARRAY_COUNT(mGroupTexels);
    if (Npcs->GetPopulationRevision() != mUploadedRevision)
    {
        UploadPopulation();
    }
    else if (RailTexture != nullptr
        && FMath::Abs(Npcs->GetRailFrame().RailTimeS - mGpuRailEpochS) > SOL::NPC_GPU_RAIL_EPOCH_S
        && FPlatformTime::Seconds() - mGpuRailEpochRealS >= SOL::NPC_GPU_RAIL_EPOCH_MIN_INTERVAL_S)
    {
        ReepochRails();
    }
    const bool bPlumes = CVarSOLNpcPlumes.GetValueOnGameThread() != 0;
    if (bPlumes != mArePlumeMeshes)
    {
        ApplyPlumeMeshes(bPlumes);
    }
    UpdateFarPoints();
    UpdateCpuShips();
}

//////////////////////////////////////////////////////////////////////////
// Switches the hull layers between the thruster meshes with their plume floats and the plain shapes without any
void ASOLNpcVisuals::ApplyPlumeMeshes(const bool bPlumes)
{
    mArePlumeMeshes = bPlumes;
    const TObjectPtr<UStaticMesh>* meshes = bPlumes ? PlumeHullMeshes : PlainHullMeshes;
    UInstancedStaticMeshComponent* hulls[2] = { HullsFaction0, HullsFaction1 };
    for (int32 faction = 0; faction < 2; ++faction)
    {
        hulls[faction]->SetStaticMesh(meshes[faction]);

        // The baseline carries no custom floats at all (its instances upload as before #51); switching back on
        // re-creates them zeroed, so nothing is stale and the next upload writes only this frame's plumes
        hulls[faction]->SetNumCustomDataFloats(bPlumes ? PLUME_FLOATS : 0);
        mPlumeWrittenRange[faction] = FIntPoint(1, 0);
    }
    for (int32 slot = 0; slot < MeshPool.Num(); ++slot)
    {
        MeshPool[slot]->SetStaticMesh(meshes[slot & 1]);
    }
}

//////////////////////////////////////////////////////////////////////////
// Builds the point block mesh (NPC_POINT_BLOCK_SHIPS triangles; vertex z = ship index in the block)
UStaticMesh* ASOLNpcVisuals::BuildPointBlockMesh()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLNpcVisuals::BuildPointBlockMesh);
    FMeshDescription description;
    FStaticMeshAttributes attributes(description);
    attributes.Register();
    const int32 shipCount = SOL::NPC_POINT_BLOCK_SHIPS;
    description.ReserveNewVertices(shipCount * 3);
    description.ReserveNewVertexInstances(shipCount * 3);
    description.ReserveNewTriangles(shipCount);
    const FPolygonGroupID polygonGroup = description.CreatePolygonGroup();
    TVertexAttributesRef<FVector3f> positions = attributes.GetVertexPositions();
    TVertexInstanceAttributesRef<FVector2f> uvs = attributes.GetVertexInstanceUVs();
    uvs.SetNumChannels(2);
    TVertexInstanceAttributesRef<FVector3f> normals = attributes.GetVertexInstanceNormals();

    // One triangle per ship: UV0 = billboard corner, UV1 = the ship's index in the block as (index % 64, index / 64)
    // (exact even in half-precision UVs); the local position is a tiny copy of the corner, z = the index
    for (int32 ship = 0; ship < shipCount; ++ship)
    {
        FVertexInstanceID corners[3];
        for (int32 corner = 0; corner < 3; ++corner)
        {
            const FVertexID vertex = description.CreateVertex();
            positions[vertex] = FVector3f(POINT_CORNERS[corner].X * POINT_CORNER_LOCAL_SCALE,
                POINT_CORNERS[corner].Y * POINT_CORNER_LOCAL_SCALE, static_cast<float>(ship));
            corners[corner] = description.CreateVertexInstance(vertex);
            uvs.Set(corners[corner], 0, POINT_CORNERS[corner]);
            uvs.Set(corners[corner], 1, FVector2f(static_cast<float>(ship % 64), static_cast<float>(ship / 64)));
            normals[corners[corner]] = FVector3f::UnitZ();
        }
        description.CreateTriangle(polygonGroup, MakeArrayView(corners, 3));
    }

    UStaticMesh* mesh = NewObject<UStaticMesh>(this, TEXT("SM_SOLNpcPointBlock"));
    mesh->GetStaticMaterials().Add(FStaticMaterial());
    // The vertex shader moves every vertex anywhere in render space, so the bounds (also each ISM instance's culling
    // bounds) cover all of it
    mesh->SetPositiveBoundsExtension(FVector(SOL::NPC_RENDER_SPACE_BOUNDS_CM));
    mesh->SetNegativeBoundsExtension(FVector(SOL::NPC_RENDER_SPACE_BOUNDS_CM));
    UStaticMesh::FBuildMeshDescriptionsParams params;
    params.bBuildSimpleCollision = false;
    params.bFastBuild = true;
    params.bAllowCpuAccess = false;
    mesh->BuildFromMeshDescriptions({ &description }, params);
    mesh->CalculateExtendedBounds();
    return mesh;
}

//////////////////////////////////////////////////////////////////////////
// Creates a float RGBA texture of the given size (nearest filtering, linear)
UTexture2D* ASOLNpcVisuals::CreateFloatTexture(const int32 width, const int32 height) const
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

//////////////////////////////////////////////////////////////////////////
// Sizes a points ISM to blockCount instances, the custom data of each being its first ship index
void ASOLNpcVisuals::SizePointBlocks(UInstancedStaticMeshComponent* component, const int32 blockCount,
    int32& inOutCurrent)
{
    if (blockCount == inOutCurrent)
    {
        return;
    }
    component->ClearInstances();
    TArray<FTransform> identity;
    identity.Init(FTransform::Identity, blockCount);
    component->AddInstances(identity, false, false, false);
    for (int32 block = 0; block < blockCount; ++block)
    {
        component->SetCustomDataValue(block, 0, static_cast<float>(block * SOL::NPC_POINT_BLOCK_SHIPS), false);
    }
    component->MarkRenderStateDirty();
    inOutCurrent = blockCount;
}

//////////////////////////////////////////////////////////////////////////
// Uploads the population's rails, re-based to the current rail time, and sizes the far blocks (once per population)
void ASOLNpcVisuals::UploadPopulation()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLNpcVisuals::UploadPopulation);
    const double uploadStartS = FPlatformTime::Seconds();
    mUploadedRevision = Npcs->GetPopulationRevision();
    const int32 shipCount = Npcs->GetRails().Num();
    if (shipCount == 0)
    {
        // No population: release the rail texture (tens of MB at 1M) and draw no far blocks or lanes
        RailTexture = nullptr;
        FarMaterial->SetTextureParameterValue(PARAM_RAIL_TEX, nullptr);
        CpuPointMaterial->SetTextureParameterValue(PARAM_RAIL_TEX, nullptr);
        SizePointBlocks(FarPoints, 0, mFarBlockCount);
        mLaneShipCounts.Reset();
        BuildLaneMesh();
        return;
    }

    // A fresh texture per population, filled through its bulk data
    const int32 width = SOL::NPC_RAIL_TEXTURE_WIDTH;
    const int32 height = FMath::DivideAndRoundUp(shipCount * 2, width);
    mGpuRailEpochS = Npcs->GetRailFrame().RailTimeS;
    mGpuRailEpochRealS = FPlatformTime::Seconds();
    RailTexture = UTexture2D::CreateTransient(width, height, PF_A32B32G32R32F);
    RailTexture->Filter = TF_Nearest;
    RailTexture->SRGB = false;
    RailTexture->CompressionSettings = TC_HDR;
    RailTexture->NeverStream = true;
    FTexture2DMipMap& mip = RailTexture->GetPlatformData()->Mips[0];
    FVector4f* texels = static_cast<FVector4f*>(mip.BulkData.Lock(LOCK_READ_WRITE));
    const double fillStartS = FPlatformTime::Seconds();
    FillRailTexels(texels, width * height, mGpuRailEpochS, &mLaneShipCounts);
    const double fillMs = (FPlatformTime::Seconds() - fillStartS) * 1000.0;
    mip.BulkData.Unlock();
    RailTexture->UpdateResource();
    FarMaterial->SetTextureParameterValue(PARAM_RAIL_TEX, RailTexture);
    CpuPointMaterial->SetTextureParameterValue(PARAM_RAIL_TEX, RailTexture);
    SizePointBlocks(FarPoints, FMath::DivideAndRoundUp(shipCount, SOL::NPC_POINT_BLOCK_SHIPS), mFarBlockCount);
    BuildLaneMesh();
    UE_LOG(LogSOL, Log, TEXT("NpcVisuals %s: uploaded %d rails (%dx%d texels, %.1f MB), %d far blocks, %d lane ribbon "
        "vertices; rail fill with the lane histogram %.2f ms, whole upload %.2f ms"), *GetName(), shipCount, width,
        height, width * height * sizeof(FVector4f) / (1024.0 * 1024.0), mFarBlockCount, mLaneMeshVertexCount, fillMs,
        (FPlatformTime::Seconds() - uploadStartS) * 1000.0);
}

//////////////////////////////////////////////////////////////////////////
// Re-uploads the rails re-based to the current rail time, in place (rail time far from the GPU epoch)
void ASOLNpcVisuals::ReepochRails()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLNpcVisuals::ReepochRails);
    const int32 width = RailTexture->GetSizeX();
    const int32 height = RailTexture->GetSizeY();
    mGpuRailEpochS = Npcs->GetRailFrame().RailTimeS;
    mGpuRailEpochRealS = FPlatformTime::Seconds();

    // A rare (rate-limited) upload: the buffer and region are owned by the render command and freed when it ran
    FVector4f* texels = new FVector4f[static_cast<SIZE_T>(width) * height];
    const double fillStartS = FPlatformTime::Seconds();
    FillRailTexels(texels, width * height, mGpuRailEpochS, nullptr);
    UE_LOG(LogSOL, Log, TEXT("NpcVisuals %s: rails re-epoched, rail fill without the lane histogram %.2f ms"),
        *GetName(), (FPlatformTime::Seconds() - fillStartS) * 1000.0);
    FUpdateTextureRegion2D* region = new FUpdateTextureRegion2D(0, 0, 0, 0, width, height);
    RailTexture->UpdateTextureRegions(0, 1, region, width * sizeof(FVector4f), sizeof(FVector4f),
        reinterpret_cast<uint8*>(texels), [](uint8* data, const FUpdateTextureRegion2D* regions)
        {
            delete[] reinterpret_cast<FVector4f*>(data);
            delete regions;
        });
}

//////////////////////////////////////////////////////////////////////////
// Fills an upload buffer with the population's re-based rails (two texels per ship) and the lane histogram if asked
void ASOLNpcVisuals::FillRailTexels(FVector4f* texels, const int32 texelCount, const double epochRailTimeS,
    TArray<int32>* outLaneShipCounts)
{
    // Two texels per ship (SOLNpcRail::GpuRailTexels: a lane ship's cycle time at the epoch as hi / lo floats and its
    // leg time); the padding is zero
    const TConstArrayView<FSOLNpcRail> rails = Npcs->GetRails();
    const int32 shipCount = FMath::Min(rails.Num(), texelCount / 2);
    constexpr int32 SHIPS_PER_TASK = 16384;
    const int32 taskCount = FMath::DivideAndRoundUp(shipCount, SHIPS_PER_TASK);

    // The lane histogram (SDD 38 D13): each task counts its own slice on the stack and writes its own counts once,
    // merged below
    const int32 laneCount = outLaneShipCounts != nullptr ? FMath::Max(Npcs->GetGroups().LaneCount, 0) : 0;
    TArray<int32> taskLaneCounts;
    taskLaneCounts.SetNumZeroed(taskCount * laneCount);
    const TConstArrayView<FSOLLaneProfile> laneProfiles = Npcs->GetGroups().LaneProfile;
    ParallelFor(taskCount, [&](const int32 block)
    {
        const int32 start = block * SHIPS_PER_TASK;
        const int32 end = FMath::Min(start + SHIPS_PER_TASK, shipCount);
        for (int32 ship = start; ship < end; ++ship)
        {
            SOLNpcRail::GpuRailTexels(rails[ship], epochRailTimeS, laneProfiles, texels[ship * 2],
                texels[ship * 2 + 1]);
        }
        if (laneCount > 0)
        {
            SOLNpcLane::AccumulateLaneShips(rails.Slice(start, end - start),
                MakeArrayView(taskLaneCounts.GetData() + block * laneCount, laneCount));
        }
    });
    if (shipCount * 2 < texelCount)
    {
        FMemory::Memzero(texels + shipCount * 2, static_cast<SIZE_T>(texelCount - shipCount * 2) * sizeof(FVector4f));
    }
    if (outLaneShipCounts != nullptr)
    {
        outLaneShipCounts->Reset();
        outLaneShipCounts->SetNumZeroed(laneCount);
        for (int32 task = 0; task < taskCount; ++task)
        {
            for (int32 lane = 0; lane < laneCount; ++lane)
            {
                (*outLaneShipCounts)[lane] += taskLaneCounts[task * laneCount + lane];
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Writes this frame's group points (relative to the viewpoint) and the point parameters
void ASOLNpcVisuals::UpdateFarPoints()
{
    const FSOLNpcRailFrame& frame = Npcs->GetRailFrame();
    const FVector3d viewpointM = Anchor->GetViewpointM();
    const int32 groupCount = FMath::Min(frame.GroupPointAM.Num(), SOL::NPC_GROUP_TEXTURE_WIDTH);

    // Group points relative to the viewpoint (float is enough: rails are evaluated relative to the viewer)
    TArray<FVector4f>& texels = mGroupTexels[mUploadRing];
    texels.SetNumZeroed(SOL::NPC_GROUP_TEXTURE_WIDTH * SOL::NPC_GROUP_TEXTURE_ROWS, EAllowShrinking::No);
    for (int32 group = 0; group < groupCount; ++group)
    {
        texels[group] = FVector4f(FVector3f(frame.GroupPointAM[group] - viewpointM), 0.0f);
        texels[SOL::NPC_GROUP_TEXTURE_WIDTH + group] = FVector4f(FVector3f(frame.GroupPointBM[group] - viewpointM),
            0.0f);
    }

    // The lanes of this frame and their data in the free .w channels (SDD 38 section 4.2): O(lanes), no allocation
    {
        SCOPE_CYCLE_COUNTER(STAT_SOL_NpcLanes);
        SOLNpcLane::BuildLaneFrame(Npcs->GetGroups(), frame, mLaneShipCounts, mLaneFrame);
        SOLNpcLane::WriteLaneGroupTexelW(mLaneFrame, texels);
    }

    // The lane traffic rows 2-5 (SDD 50 section 4.6): basis, wall radius, U-turn and leg data per lane
    SOLNpcRail::WriteLaneTrafficTexels(frame, texels);
    mGroupRegions[mUploadRing] = FUpdateTextureRegion2D(0, 0, 0, 0, SOL::NPC_GROUP_TEXTURE_WIDTH,
        SOL::NPC_GROUP_TEXTURE_ROWS);
    GroupTexture->UpdateTextureRegions(0, 1, &mGroupRegions[mUploadRing],
        SOL::NPC_GROUP_TEXTURE_WIDTH * sizeof(FVector4f), sizeof(FVector4f),
        reinterpret_cast<uint8*>(texels.GetData()));

    // Parameters shared by both point layers
    const FSOLNpcStats& stats = Npcs->GetStats();
    const FVector viewRenderCm = Anchor->UniverseToRenderCm(viewpointM);
    const FLinearColor params1(viewRenderCm.X, viewRenderCm.Y, viewRenderCm.Z,
        static_cast<float>(SOL::DEFAULT_MAX_RENDER_DISTANCE_CM));
    const FLinearColor params2(static_cast<float>(SOL::NPC_POINT_MIN_PIXELS),
        static_cast<float>(SOL::NPC_HULL_LENGTH_M * 0.5), static_cast<float>(SOL::NPC_POINT_BRIGHTNESS_REF_M),
        static_cast<float>(SOL::NPC_POINT_BRIGHTNESS_MIN));
    const FLinearColor params3(static_cast<float>(SOL::NPC_POINT_BRIGHTNESS_EXPONENT),
        static_cast<float>(SOL::NPC_POINT_EMISSIVE), static_cast<float>(SOL::NPC_RAIL_TEXTURE_WIDTH),
        static_cast<float>(SOL::NPC_CPU_TEXTURE_WIDTH));
    for (UMaterialInstanceDynamic* material : { FarMaterial.Get(), CpuPointMaterial.Get() })
    {
        material->SetVectorParameterValue(PARAM_1, params1);
        material->SetVectorParameterValue(PARAM_2, params2);
        material->SetVectorParameterValue(PARAM_3, params3);
    }
    FarMaterial->SetVectorParameterValue(PARAM_0, FLinearColor(static_cast<float>(frame.RailTimeS - mGpuRailEpochS),
        static_cast<float>(stats.MidRadiusM), static_cast<float>(Npcs->GetRails().Num()), 0.0f));
    UpdateLanes(params1);
}

//////////////////////////////////////////////////////////////////////////
// Loads the lane material and sets its constant parameters; false if it is missing
bool ASOLNpcVisuals::SetUpLanes()
{
    using namespace SOL::NpcLaneMaterialParams;
    using namespace SOL::Hologram;
    UMaterialInterface* laneMaterial = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::NPC_LANES_MATERIAL);
    if (laneMaterial == nullptr)
    {
        return false;
    }
    LaneMaterial = UMaterialInstanceDynamic::Create(laneMaterial, this);
    LaneMaterial->SetTextureParameterValue(FName(GROUP_TEXTURE), GroupTexture);
    LaneMaterial->SetTextureParameterValue(FName(TUNNEL_TEXTURE), Tunnels->GetTunnelTexture());

    // The tunables go to the material once (the shader keeps no copy of them, SOLNpcLane.ush)
    LaneMaterial->SetVectorParameterValue(FName(STYLE0), FLinearColor(static_cast<float>(SOL::NPC_LANE_WIDTH_MIN_PX),
        static_cast<float>(SOL::NPC_LANE_WIDTH_MAX_PX), static_cast<float>(SOL::NPC_LANE_GAP_PX),
        static_cast<float>(SOL::NPC_LANE_GLOW_PAD_PX)));
    LaneMaterial->SetVectorParameterValue(FName(STYLE1), FLinearColor(static_cast<float>(SOL::NPC_LANE_BRIGHTNESS_MIN),
        static_cast<float>(SOL::NPC_LANE_BRIGHTNESS_MAX), static_cast<float>(SOL::NPC_LANE_DASH_BASE_COUNT),
        static_cast<float>(SOL::NPC_LANE_DASH_DUTY)));
    LaneMaterial->SetVectorParameterValue(FName(STYLE2), FLinearColor(
        static_cast<float>(SOL::NPC_LANE_REFERENCE_HEIGHT_PX), static_cast<float>(SOL::NPC_LANE_EMISSIVE),
        static_cast<float>(SOL::NPC_LANE_DASH_MIN_LEVEL), 0.0f));
    LaneMaterial->SetVectorParameterValue(FName(GLOW0), FLinearColor(static_cast<float>(HUD_GLOW_CORE_HALF_PX),
        static_cast<float>(HUD_GLOW_CORE_FALLOFF), static_cast<float>(HUD_GLOW_CORE_FALLOFF_MAX),
        static_cast<float>(HUD_GLOW_HALO_WEIGHT)));
    LaneMaterial->SetVectorParameterValue(FName(GLOW1), FLinearColor(static_cast<float>(HUD_GLOW_HALO_SCALE),
        static_cast<float>(HUD_GLOW_HALO_POWER), HUD_GLOW_SCALE_MAX, 0.0f));
    LaneMaterial->SetVectorParameterValue(FName(OUTBOUND_COLOR), FLinearColor(SOL::NPC_LANE_OUTBOUND_COLOR));
    LaneMaterial->SetVectorParameterValue(FName(INBOUND_COLOR), FLinearColor(SOL::NPC_LANE_INBOUND_COLOR));
    mLaneGlowFeed.Reset();

    // The ribbon mesh: rebuilt in place per population; never ray traced
    LaneMesh = NewObject<UStaticMesh>(this, NAME_None, RF_Transient);
    LaneMesh->SetStaticMaterials({ FStaticMaterial(LaneMaterial) });
    LaneMesh->bSupportRayTracing = false;
    LaneLines->SetMaterial(0, LaneMaterial);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Builds the lane ribbon mesh for the current population's lane ship counts (once per population)
void ASOLNpcVisuals::BuildLaneMesh()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLNpcVisuals::BuildLaneMesh);
    TArray<FVector2f> uv0;
    TArray<FVector2f> uv1;
    TArray<uint32> indices;
    SOLNpcLane::BuildLaneRibbonLayout(mLaneShipCounts, uv0, uv1, indices);
    mLaneMeshVertexCount = LaneMesh != nullptr ? uv0.Num() : 0;
    if (mLaneMeshVertexCount == 0)
    {
        return;
    }

    // Every position is zero (the material places the vertices); a fixed basis, UV0 and UV1 from the layout
    TArray<FStaticMeshBuildVertex> vertices;
    vertices.SetNumZeroed(mLaneMeshVertexCount);
    for (int32 index = 0; index < mLaneMeshVertexCount; ++index)
    {
        FStaticMeshBuildVertex& vertex = vertices[index];
        vertex.TangentX = FVector3f::UnitX();
        vertex.TangentY = FVector3f::UnitY();
        vertex.TangentZ = FVector3f::UnitZ();
        vertex.UVs[0] = uv0[index];
        vertex.UVs[1] = uv1[index];
        vertex.Color = FColor::White;
    }
    SOLOrbitLineMesh::ApplyRibbonToMesh(*LaneMesh, vertices, indices);
    if (LaneLines->GetStaticMesh() != LaneMesh)
    {
        LaneLines->SetStaticMesh(LaneMesh);
        LaneLines->SetMaterial(0, LaneMaterial);
    }
}

//////////////////////////////////////////////////////////////////////////
// Per frame: lanes shown, the far points' crossfade, the lane component, the flow clock, the glow and the frame
void ASOLNpcVisuals::UpdateLanes(const FLinearColor& frameParams)
{
    SCOPE_CYCLE_COUNTER(STAT_SOL_NpcLanes);

    // Shown: the Lanes row (IsOverlayShown folds in the master O) or the verification override, and sol.NpcLanes
    const bool bHasSettings = mIsLaneOverride || Settings != nullptr;
    const bool bRowShown = mIsLaneOverride ? mIsLaneOverrideShown
        : Settings != nullptr && Settings->IsOverlayShown(SOLOverlay::EOverlay::Lanes);
    mAreLanesShown = SOLNpcLane::AreLanesShown(bHasSettings, bRowShown, CVarSOLNpcLanes.GetValueOnGameThread() != 0);

    // The far points' crossfade (D9, D10): while the lanes are shown; in the flight view the line is gone (SDD 52 D1)
    // and the crossfade keeps far lane ships hidden, the rings standing for the lane (D6)
    const bool bCrossfade = mAreLanesShown && LaneMaterial != nullptr && mLaneMeshVertexCount > 0;
    if (bCrossfade != mIsPointCrossfadeOn || !mHasPushedPointCrossfade)
    {
        mIsPointCrossfadeOn = bCrossfade;
        mHasPushedPointCrossfade = true;
        FarMaterial->SetVectorParameterValue(PARAM_POINT_LANES, SOLNpcLane::PointLaneParams(bCrossfade));
    }

    // The component: shown with a drawn lane, on the jump map only (SDD 52 D1)
    const APlayerController* controller = GetWorld()->GetFirstPlayerController();
    const ASOLShipPawn* pawn = controller != nullptr ? Cast<ASOLShipPawn>(controller->GetPawn()) : nullptr;
    const bool bMapOpen = pawn != nullptr && pawn->IsMapOpen();
    const bool bDraw = bCrossfade && SOLNpcLane::ShouldDrawLaneLines(mAreLanesShown, bMapOpen, mLaneFrame);
    if (bDraw != mAreLaneLinesDrawn)
    {
        mAreLaneLinesDrawn = bDraw;
        LaneLines->SetVisibility(bDraw);
    }

    // The tunnels at every distance in the flight view (SDD 49, SDD 52 D1): hidden on the jump map (Q6), oriented by
    // the camera for their labels
    const FVector forwardUnreal = controller != nullptr && controller->PlayerCameraManager != nullptr
        ? controller->PlayerCameraManager->GetCameraRotation().Vector() : FVector::ForwardVector;
    Tunnels->UpdateTunnels(mLaneFrame, Npcs->GetGroups(), Anchor->GetViewpointM(),
        FVector3d(forwardUnreal.X, -forwardUnreal.Y, forwardUnreal.Z), mAreLanesShown, bMapOpen, frameParams,
        GFrameCounter);

    // The orbit lines keep the shared occluders current while lane lines or tunnels draw (SDD 38 D24, SDD 49 D12)
    const bool bOverlayDrawn = SOLNpcTunnel::IsLaneOverlayDrawn(bDraw, Tunnels->AreTunnelsDrawn());
    if (bOverlayDrawn != mIsLaneOverlayDrawn)
    {
        mIsLaneOverlayDrawn = bOverlayDrawn;
        if (IsValid(OrbitLines))
        {
            OrbitLines->SetLanesDrawn(bOverlayDrawn);
        }
    }

    // The dash flow: real time (no time warp), never held, the pause menu included (D15, amended 2026-11-11)
    mLaneFlowClockS = SOLNpcLane::AdvanceLaneFlowClockS(mLaneFlowClockS,
        FMath::Min(FApp::GetDeltaTime(), SOL::MAX_FRAME_DELTA_S));

    // The HUD glow (pending or applied) to the lane and tunnel materials, only when it changes while one draws
    if (bOverlayDrawn && mLaneGlowFeed.Update(Settings != nullptr ? Settings->GetLiveValues().HudGlow
        : SOL::Settings::HUD_GLOW_DEFAULT))
    {
        if (LaneMaterial != nullptr)
        {
            LaneMaterial->SetScalarParameterValue(PARAM_LANE_GLOW_SCALE, mLaneGlowFeed.GetScale());
        }
        Tunnels->SetGlowScale(mLaneGlowFeed.GetScale());
    }
    if (!bDraw)
    {
        return;
    }

    // The lane material: the frame and flow every frame
    LaneMaterial->SetVectorParameterValue(PARAM_LANE_FRAME, frameParams);
    LaneMaterial->SetVectorParameterValue(PARAM_LANE_0,
        SOLNpcLane::LaneMaterialParams(SOLNpcLane::LaneDashFlowPhase(mLaneFlowClockS)));
}

//////////////////////////////////////////////////////////////////////////
// Places the CPU ships: mesh pool, instanced hulls and CPU points
void ASOLNpcVisuals::UpdateCpuShips()
{
    const TConstArrayView<FSOLNpcCpuShip> ships = Npcs->GetCpuShips();
    const FSOLRenderOrigin& origin = Anchor->GetRenderOrigin();
    const FVector3d viewpointM = Anchor->GetViewpointM();
    TArray<FVector4f>& cpuTexels = mCpuTexels[mUploadRing];
    cpuTexels.Reset();
    for (int32 faction = 0; faction < 2; ++faction)
    {
        mHullTransforms[faction].Reset();
        mHullPrevTransforms[faction].Reset();
    }
    const double lengthCm = SOL::NPC_HULL_LENGTH_M * SOL::METERS_TO_CM;
    const double pixelPerCm = PixelWorldCm(1.0);
    const FVector viewRenderCm(origin.UniverseToRenderCm(viewpointM));
    double fovDeg = 0.0;
    FVector2D viewportPx;
    ViewCamera(fovDeg, viewportPx);
    mPlumeFocalRefPx = SOLThruster::FocalRefPx(fovDeg, viewportPx.X / viewportPx.Y);
    const bool bPlumes = mArePlumeMeshes;

    // Returns a hull-drawn ship's plume floats from its main-engine thrust and its true hull length at a render
    // distance (SDD 51 sections 4.3, 4.5; dot-drawn ships never get here, Q27)
    const auto shipPlume = [this, bPlumes](const FSOLNpcCpuShip& ship, const double distanceCm)
    {
        return bPlumes ? SOLThruster::PlumeInstance(ship.ThrustLocalG.X, false, SOL::Thruster::NPC_HULL_LENGTH_M,
            SOL::Thruster::NPC_NOZZLE_RADIUS_M, distanceCm / SOL::METERS_TO_CM, mPlumeFocalRefPx) : FSOLPlumeInstance();
    };
    const float gpuHideRadiusM = static_cast<float>(Npcs->GetStats().MidRadiusM);
    int32 meshSlots[2] = { 0, 1 };

    // The thruster flames (SDD 51 R7, R2, R4, R10): fed from this pass for the hull-drawn ships; switches read once
    // here, never in the parallel tasks. Off with the plume meshes too (sol.NpcPlumes 0 collects no thrust)
    ASOLThrusterRibbons* const ribbons = bPlumes && IsValid(ThrusterRibbons)
        && ASOLThrusterRibbons::AreRibbonsEnabled() ? ThrusterRibbons.Get() : nullptr;
    const double ribbonVanishPx = ribbons != nullptr ? ribbons->GetVanishPx() : 0.0;
    const double focalRefPx = mPlumeFocalRefPx;

    // The farthest an NPC flame reaches from its ship's centre: the aft nozzle plus the longest flame with a bend margin
    static constexpr double NPC_FLAME_MAX_REACH_M = -SOL::Thruster::NPC_NOZZLE_X_M
        + SOL::Thruster::FLAME_REACH_BEND_MARGIN * SOL::Thruster::FLAME_LENGTH_FACTOR
        * SOL::Thruster::NPC_HULL_LENGTH_M;

    // Feeds a hull-drawn ship's flame from its raw main-engine thrust (not the glow's pixel-gated PlumeInstance), only
    // while the ship's largest flame touches last frame's view frustum (R10-Q8: a ship turning into view grows its
    // flame in at v_e over 1-3 frames; off screen its flame is released once its gas has left): a new flame only for a
    // burning ship whose flame reaches V on screen (left pending for the serial commit), a ship with a flame always,
    // committed in place from this task (the store runs the spool-down). Positions in render-space metres (Unreal
    // axes, after the rebase), the nozzle offset and exhaust in non-rotating world axes (one nozzle on the aft centre)
    const auto feedRibbon = [ribbons, ribbonVanishPx, focalRefPx](const FSOLNpcCpuShip& ship,
        const FVector& locationCm, const FVector& prevLocationCm, const double distanceCm, FSOLRibbonFeed& outPending)
    {
        outPending.bValid = false;
        const double gMain = static_cast<double>(ship.ThrustLocalG.X);
        const double plumeM = SOLThruster::PlumeLengthM(SOL::Thruster::NPC_HULL_LENGTH_M, gMain);
        const double brightness = SOLThruster::PlumeBrightness(gMain);
        const double distanceM = distanceCm / SOL::METERS_TO_CM;
        const bool bBurning = plumeM > 0.0 && brightness > 0.0;
        const bool bReachesV = bBurning
            && SOLThruster::RibbonPixelLength(plumeM, distanceM, focalRefPx) >= ribbonVanishPx;
        const int32 trailSlot = ribbons->FindTrail(ship.NpcIndex);
        if (!bReachesV && trailSlot == INDEX_NONE)
        {
            return;
        }
        const FVector3d shipM = FVector3d(locationCm) / SOL::METERS_TO_CM;
        if (!ribbons->IsOnScreen(shipM, NPC_FLAME_MAX_REACH_M))
        {
            return;
        }
        const FVector3d forward = (ship.HeadingDirection.IsZero() ? FVector3d(locationCm - prevLocationCm)
            : SOLRender::EclipticToUnreal(FVector3d(ship.HeadingDirection))).GetSafeNormal();
        if (forward.IsZero())
        {
            return;
        }
        const FSOLNozzle& nozzle = SOLThruster::HullThrusters((ship.Faction & 1) != 0 ? ESOLHullKind::NpcFaction1
            : ESOLHullKind::NpcFaction0).Nozzles[0];
        const FVector3d offsetM = forward * static_cast<double>(nozzle.PositionM.X);
        FSOLRibbonFeed feed;
        feed.Key = ship.NpcIndex;
        feed.ShipPositionM = shipM;
        feed.Pose.OffsetM = FVector3f(offsetM);
        feed.Pose.ExhaustDir = FVector3f(-forward);
        if (bBurning)
        {
            feed.ColourScalar = static_cast<float>(SOLThruster::PlumeColourScalar(gMain, false));
            feed.PlumeLengthM = plumeM;
            feed.Pose.Brightness = static_cast<float>(brightness);
        }
        feed.DistanceM = distanceM;
        feed.NozzleRadiusM = nozzle.RadiusM;
        feed.bValid = true;
        ribbons->FeedNpc(trailSlot, feed, outPending);
    };
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLNpcVisuals::UpdateCpuShips);

    // The Mid hand-over of the Point-tier ships (SDD 50 section 4.6), in parallel before the slots: a lane ship is
    // drawn here exactly where the point shader hides it (the float mirror of SOLNpcTraffic.ush on this frame's GPU
    // inputs, its length against the Mid radius) and blended from the mirror's position at the Mid radius to the
    // double one at the hull radius; other ships by their bare-rail distance at their double position. Per ship: the
    // two GPU rail texels and one float evaluation, no allocation
    const TConstArrayView<FSOLNpcRail> rails = Npcs->GetRails();
    const TConstArrayView<FSOLLaneProfile> laneProfiles = Npcs->GetGroups().LaneProfile;
    const TConstArrayView<FVector4f> groupTexels = mGroupTexels[mUploadRing];
    const bool bHasGroupRows = groupTexels.Num() >= SOL::NPC_GROUP_TEXTURE_WIDTH * SOL::NPC_GROUP_TEXTURE_ROWS;
    const float shaderTimeS = static_cast<float>(Npcs->GetRailFrame().RailTimeS - mGpuRailEpochS);
    const double hullStartM = Npcs->GetStats().InstancedRadiusM;
    const uint8 laneKind = static_cast<uint8>(SOLNpcScale::EPlacementKind::Lane);
    constexpr int32 SHIPS_PER_TASK = 1024;
    mPointRelM.SetNumUninitialized(ships.Num(), EAllowShrinking::No);
    ParallelFor(FMath::DivideAndRoundUp(ships.Num(), SHIPS_PER_TASK), [&](const int32 block)
    {
        const int32 end = FMath::Min((block + 1) * SHIPS_PER_TASK, ships.Num());
        for (int32 index = block * SHIPS_PER_TASK; index < end; ++index)
        {
            const FSOLNpcCpuShip& ship = ships[index];
            if (ship.RenderTier != static_cast<uint8>(SOLNpcScale::ERenderTier::Point) || ship.bHidden != 0)
            {
                continue;
            }
            const FVector3d cpuRelM = ship.PositionM - viewpointM;
            const FSOLNpcRail* rail = rails.IsValidIndex(ship.NpcIndex) ? &rails[ship.NpcIndex] : nullptr;
            if (bHasGroupRows && rail != nullptr && rail->Kind == laneKind && laneProfiles.IsValidIndex(rail->Group)
                && rail->Group < SOL::NPC_GROUP_TEXTURE_WIDTH)
            {
                // The shader's inputs: the rail texels at the GPU epoch and the lane column's six rows as uploaded
                FVector4f texelA;
                FVector4f texelB;
                SOLNpcRail::GpuRailTexels(*rail, mGpuRailEpochS, laneProfiles, texelA, texelB);
                FVector4f rows[SOL::NPC_GROUP_TEXTURE_ROWS];
                for (int32 row = 0; row < SOL::NPC_GROUP_TEXTURE_ROWS; ++row)
                {
                    rows[row] = groupTexels[row * SOL::NPC_GROUP_TEXTURE_WIDTH + rail->Group];
                }
                const SOLNpcTrafficMirror::FHandOver handOver = SOLNpcTrafficMirror::CpuPointHandOver(
                    SOLNpcTrafficMirror::LaneShipRelM(texelA, texelB, rows, shaderTimeS), cpuRelM, gpuHideRadiusM,
                    hullStartM);
                mPointRelM[index] = FVector4f(handOver.RelM, handOver.bCpuDraws ? 1.0f : 0.0f);
                continue;
            }
            mPointRelM[index] = FVector4f(FVector3f(cpuRelM), ship.RailDistanceM < gpuHideRadiusM ? 1.0f : 0.0f);
        }
    });

    // Returns a ship's render location, previous location and drawn hull rotation from the collected heading (a Full
    // ship's nose, a Mid ship's rail plus deviation velocity; the motion since last frame if none) and body top, both
    // converted to Unreal axes
    const auto placeShip = [&origin](const FSOLNpcCpuShip& ship, const int32 faction, FVector& outLocationCm,
        FVector& outPrevLocationCm, FQuat& outRotation)
    {
        outLocationCm = FVector(origin.UniverseToRenderCm(ship.PositionM));
        outPrevLocationCm = FVector(origin.UniverseToRenderCm(ship.PrevPositionM));
        const FVector heading = ship.HeadingDirection.IsZero() ? outLocationCm - outPrevLocationCm
            : SOLRender::EclipticToUnreal(FVector3d(ship.HeadingDirection));
        outRotation = SOLNpcAi::HullDrawRotation(heading, SOLRender::EclipticToUnreal(FVector3d(ship.HeadingTop)),
            faction);
    };

    // Serial pass: each ship's slot in its layer (texel or per-faction instance; mesh-pool ships are placed here,
    // INDEX_NONE), so the heavy per-ship work below can run in parallel (SDD 11 Amendment 6)
    mShipSlots.SetNumUninitialized(ships.Num(), EAllowShrinking::No);
    mMeshPoolShips.Reset();
    int32 pointCount = 0;
    int32 instanceCounts[2] = { 0, 0 };
    for (int32 index = 0; index < ships.Num(); ++index)
    {
        const FSOLNpcCpuShip& ship = ships[index];
        const int32 faction = ship.Faction & 1;
        if (ship.bHidden != 0)
        {
            mShipSlots[index] = INDEX_NONE;
            continue;
        }
        if (ship.RenderTier == static_cast<uint8>(SOLNpcScale::ERenderTier::Point))
        {
            // At or beyond the Mid radius the far layer draws this ship's rail point (Mid hysteresis band): drawing it
            // here too would double it (the hand-over pass above decided by the far layer's own test)
            mShipSlots[index] = mPointRelM[index].W > 0.5f ? pointCount++ : INDEX_NONE;
            continue;
        }
        if (ship.RenderTier == static_cast<uint8>(SOLNpcScale::ERenderTier::Mesh)
            && meshSlots[faction] < NPC_MESH_POOL_SIZE)
        {
            FVector locationCm, prevLocationCm;
            FQuat rotation;
            placeShip(ship, faction, locationCm, prevLocationCm, rotation);
            const int32 poolSlot = meshSlots[faction];
            UStaticMeshComponent* mesh = MeshPool[poolSlot];
            meshSlots[faction] += 2;
            mesh->SetWorldTransform(HullTransform(locationCm, rotation, faction, lengthCm));
            float plume[PLUME_FLOATS];
            SOLThruster::PlumeCustomData(shipPlume(ship, (locationCm - viewRenderCm).Size()),
                MakeArrayView(plume, PLUME_FLOATS));
            mesh->SetCustomPrimitiveDataFloatArray(0, MakeArrayView(plume, PLUME_FLOATS));
            if (mMeshPoolOwners[poolSlot] != ship.NpcIndex)
            {
                // A new owner: no motion vector from where the previous ship was
                mesh->ResetSceneVelocity();
                mMeshPoolOwners[poolSlot] = ship.NpcIndex;
            }
            mesh->SetVisibility(true);
            mMeshPoolShips.Add(index);
            mShipSlots[index] = INDEX_NONE;
            continue;
        }
        mShipSlots[index] = instanceCounts[faction]++;
    }
    cpuTexels.SetNumUninitialized(pointCount, EAllowShrinking::No);
    const int32 blockCount = FMath::DivideAndRoundUp(ships.Num(), SHIPS_PER_TASK);
    for (int32 faction = 0; faction < 2; ++faction)
    {
        mHullTransforms[faction].SetNumUninitialized(instanceCounts[faction], EAllowShrinking::No);
        mHullPrevTransforms[faction].SetNumUninitialized(instanceCounts[faction], EAllowShrinking::No);
        mHullPlumeData[faction].SetNumUninitialized(instanceCounts[faction] * PLUME_FLOATS, EAllowShrinking::No);
        mPlumeBlockRanges[faction].SetNumUninitialized(blockCount, EAllowShrinking::No);
    }

    // Ribbon feed slots: the pool meshes' ships first (fed here, serially), then faction 0's and faction 1's instance
    // slots (fed by the parallel pass, one slot per ship)
    const int32 meshFeeds = mMeshPoolShips.Num();
    const int32 instanceFeedBase[2] = { meshFeeds, meshFeeds + instanceCounts[0] };
    const TArrayView<FSOLRibbonFeed> ribbonFeeds = ribbons != nullptr
        ? ribbons->BeginNpcFeeds(meshFeeds + instanceCounts[0] + instanceCounts[1]) : TArrayView<FSOLRibbonFeed>();
    if (ribbons != nullptr)
    {
        for (int32 feed = 0; feed < meshFeeds; ++feed)
        {
            const FSOLNpcCpuShip& ship = ships[mMeshPoolShips[feed]];
            FVector locationCm, prevLocationCm;
            FQuat rotation;
            placeShip(ship, ship.Faction & 1, locationCm, prevLocationCm, rotation);
            feedRibbon(ship, locationCm, prevLocationCm, (locationCm - viewRenderCm).Size(), ribbonFeeds[feed]);
        }
    }

    // Parallel pass: point texels and instanced hulls (never smaller than a few pixels, previous transform from the
    // ship's own last position) with their plume floats; each task notes its lowest and highest slot with a plume
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLNpcVisuals::PlaceShips);
    ParallelFor(blockCount, [&](const int32 block)
    {
        FIntPoint plumeRanges[2] = { FIntPoint(MAX_int32, INDEX_NONE), FIntPoint(MAX_int32, INDEX_NONE) };
        const int32 end = FMath::Min((block + 1) * SHIPS_PER_TASK, ships.Num());
        for (int32 index = block * SHIPS_PER_TASK; index < end; ++index)
        {
            const FSOLNpcCpuShip& ship = ships[index];
            const int32 slot = mShipSlots[index];
            const int32 faction = ship.Faction & 1;
            if (slot == INDEX_NONE)
            {
                continue;
            }
            if (ship.RenderTier == static_cast<uint8>(SOLNpcScale::ERenderTier::Point))
            {
                const FVector4f& relM = mPointRelM[index];
                cpuTexels[slot] = FVector4f(relM.X, relM.Y, relM.Z, static_cast<float>(faction));
                continue;
            }
            FVector locationCm, prevLocationCm;
            FQuat rotation;
            placeShip(ship, faction, locationCm, prevLocationCm, rotation);
            const double distanceCm = FMath::Max((locationCm - viewRenderCm).Size(), 1.0);
            const double sizeCm = FMath::Max(lengthCm, SOL::NPC_HULL_MIN_PIXELS * pixelPerCm * distanceCm);
            mHullTransforms[faction][slot] = HullTransform(locationCm, rotation, faction, sizeCm);
            mHullPrevTransforms[faction][slot] = HullTransform(prevLocationCm, rotation, faction, sizeCm);

            // Plume floats (SDD 51 section 4.10): the true plume, judged by its true size, not the inflated hull's
            const FSOLPlumeInstance plume = shipPlume(ship, distanceCm);
            SOLThruster::PlumeCustomData(plume, MakeArrayView(mHullPlumeData[faction].GetData() + slot * PLUME_FLOATS,
                PLUME_FLOATS));
            if (plume.Brightness > 0.0f)
            {
                plumeRanges[faction].X = FMath::Min(plumeRanges[faction].X, slot);
                plumeRanges[faction].Y = FMath::Max(plumeRanges[faction].Y, slot);
            }
            if (ribbons != nullptr)
            {
                feedRibbon(ship, locationCm, prevLocationCm, distanceCm, ribbonFeeds[instanceFeedBase[faction] + slot]);
            }
        }
        mPlumeBlockRanges[0][block] = plumeRanges[0];
        mPlumeBlockRanges[1][block] = plumeRanges[1];
    });

    // This frame's plume slot range per faction, from the tasks' ranges
    for (int32 faction = 0; faction < 2; ++faction)
    {
        FIntPoint range(MAX_int32, INDEX_NONE);
        for (const FIntPoint& blockRange : mPlumeBlockRanges[faction])
        {
            range.X = FMath::Min(range.X, blockRange.X);
            range.Y = FMath::Max(range.Y, blockRange.Y);
        }
        mPlumeRange[faction] = range.X <= range.Y ? range : FIntPoint(1, 0);
    }

    if (ribbons != nullptr)
    {
        ribbons->CommitNpcFeeds();
    }
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLNpcVisuals::UploadShips);

    // Hide the unused pool meshes
    for (int32 faction = 0; faction < 2; ++faction)
    {
        for (int32 slot = meshSlots[faction]; slot < NPC_MESH_POOL_SIZE; slot += 2)
        {
            mMeshPoolOwners[slot] = INDEX_NONE;
            if (MeshPool[slot]->IsVisible())
            {
                MeshPool[slot]->SetVisibility(false);
            }
        }
    }

    // Instanced hulls
    UploadHulls(0);
    UploadHulls(1);

    // CPU points: positions relative to the viewpoint into the CPU texture (grown in rows as needed)
    const int32 width = SOL::NPC_CPU_TEXTURE_WIDTH;
    const int32 rows = FMath::Max(FMath::DivideAndRoundUp(pointCount, width), 1);
    if (CpuTexture == nullptr || rows > mCpuTextureRows)
    {
        mCpuTextureRows = FMath::RoundUpToPowerOfTwo(rows);
        CpuTexture = CreateFloatTexture(width, mCpuTextureRows);
        CpuPointMaterial->SetTextureParameterValue(PARAM_CPU_TEX, CpuTexture);
    }
    if (pointCount > 0)
    {
        cpuTexels.SetNumZeroed(rows * width, EAllowShrinking::No);
        mCpuRegions[mUploadRing] = FUpdateTextureRegion2D(0, 0, 0, 0, width, rows);
        CpuTexture->UpdateTextureRegions(0, 1, &mCpuRegions[mUploadRing], width * sizeof(FVector4f),
            sizeof(FVector4f), reinterpret_cast<uint8*>(cpuTexels.GetData()));
    }
    SizePointBlocks(CpuPoints, FMath::DivideAndRoundUp(pointCount, SOL::NPC_POINT_BLOCK_SHIPS), mCpuBlockCount);
    CpuPointMaterial->SetVectorParameterValue(PARAM_0, FLinearColor(0.0f, 0.0f, static_cast<float>(pointCount),
        1.0f));
}

//////////////////////////////////////////////////////////////////////////
// Updates one faction's hull instances: grows or shrinks the count and writes only the used range
void ASOLNpcVisuals::UploadHulls(const int32 faction)
{
    UInstancedStaticMeshComponent* hulls = faction == 0 ? HullsFaction0.Get() : HullsFaction1.Get();
    TArray<FTransform>& transforms = mHullTransforms[faction];
    TArray<FTransform>& prevTransforms = mHullPrevTransforms[faction];
    const FTransform hidden(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
    const int32 used = transforms.Num();
    int32& capacity = mHullInstanceCounts[faction];

    // Shrink when the use stayed far below the instance count for a while (e.g. after a battle or a stop): the
    // instances are rebuilt hidden at twice the use, so last frame's range needs no rewrite
    const bool bFarBelow = capacity > SOL::NPC_HULL_MIN_INSTANCES && used * 4 < capacity;
    mHullLowFrames[faction] = bFarBelow ? mHullLowFrames[faction] + 1 : 0;
    if (mHullLowFrames[faction] >= SOL::NPC_HULL_SHRINK_FRAMES)
    {
        mHullLowFrames[faction] = 0;
        hulls->ClearInstances();
        capacity = 0;
        mHullUsed[faction] = 0;
        mPlumeWrittenRange[faction] = FIntPoint(1, 0);    // The rebuilt instances' floats are all zero
        if (used > 0)
        {
            TArray<FTransform> added;
            added.Init(hidden, FMath::Max(used * 2, SOL::NPC_HULL_MIN_INSTANCES));
            hulls->AddInstances(added, false, false, false);
            capacity = added.Num();
        }
    }

    // Grow (doubling) when needed
    if (used > capacity)
    {
        TArray<FTransform> added;
        added.Init(hidden, FMath::Max(used, capacity * 2) - capacity);
        hulls->AddInstances(added, false, false, false);
        capacity += added.Num();
    }

    // Write this frame's ships plus the ones shown last frame and not now (hidden with zero scale); nothing at all
    // when neither frame shows a hull
    const int32 range = FMath::Max(used, mHullUsed[faction]);
    mHullUsed[faction] = used;
    UploadHullPlumes(faction, range);
    if (range == 0)
    {
        return;
    }
    transforms.SetNum(range, EAllowShrinking::No);
    prevTransforms.SetNum(range, EAllowShrinking::No);
    for (int32 slot = used; slot < range; ++slot)
    {
        transforms[slot] = hidden;
        prevTransforms[slot] = hidden;
    }
    hulls->BatchUpdateInstancesTransforms(0, transforms, prevTransforms, false, false, true);
}

//////////////////////////////////////////////////////////////////////////
// Returns the world size of one pixel at a render distance (cm), from the player camera
double ASOLNpcVisuals::PixelWorldCm(const double distanceCm) const
{
    double fovDeg = 0.0;
    FVector2D viewportPx;
    ViewCamera(fovDeg, viewportPx);

    // The camera's FOV is horizontal, so one pixel spans the view width over the viewport's pixel width (square
    // pixels, any aspect)
    return 2.0 * distanceCm * FMath::Tan(FMath::DegreesToRadians(fovDeg) * 0.5) / viewportPx.X;
}

//////////////////////////////////////////////////////////////////////////
// Writes the player camera's horizontal FOV and the viewport size (90 degrees and 1920 x 1080 without either)
void ASOLNpcVisuals::ViewCamera(double& outFovDeg, FVector2D& outViewportPx) const
{
    outFovDeg = 90.0;
    outViewportPx = FVector2D(SOL::NPC_REFERENCE_VIEWPORT_WIDTH_PX, SOL::NPC_LANE_REFERENCE_HEIGHT_PX);
    const APlayerController* controller = GetWorld()->GetFirstPlayerController();
    if (controller != nullptr && controller->PlayerCameraManager != nullptr)
    {
        outFovDeg = controller->PlayerCameraManager->GetFOVAngle();
    }
    if (GetWorld()->GetGameViewport() != nullptr)
    {
        FVector2D size;
        GetWorld()->GetGameViewport()->GetViewportSize(size);
        if (size.X > 0.0 && size.Y > 0.0)
        {
            outViewportPx = size;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Writes the plume floats of the slots with a plume this frame or last (outside that union every instance holds
// zeros); the hidden slots past this frame's use hold zeros
void ASOLNpcVisuals::UploadHullPlumes(const int32 faction, const int32 range)
{
    UInstancedStaticMeshComponent* hulls = faction == 0 ? HullsFaction0.Get() : HullsFaction1.Get();
    TArray<float>& data = mHullPlumeData[faction];
    const FIntPoint now = mPlumeRange[faction];
    const FIntPoint written = mPlumeWrittenRange[faction];
    mPlumeWrittenRange[faction] = now;
    const int32 low = now.X <= now.Y ? (written.X <= written.Y ? FMath::Min(now.X, written.X) : now.X) : written.X;
    const int32 high = FMath::Min(now.X <= now.Y ? (written.X <= written.Y ? FMath::Max(now.Y, written.Y) : now.Y)
        : written.Y, hulls->GetInstanceCount() - 1);
    if (low > high || hulls->NumCustomDataFloats != PLUME_FLOATS)
    {
        return;
    }

    // Slots past this frame's ships (hidden this frame, or stale after a mesh switch) are written as zeros
    const int32 usedFloats = data.Num();
    const int32 neededFloats = FMath::Max(range, high + 1) * PLUME_FLOATS;
    if (neededFloats > usedFloats)
    {
        data.SetNumUninitialized(neededFloats, EAllowShrinking::No);
        FMemory::Memzero(data.GetData() + usedFloats, (neededFloats - usedFloats) * sizeof(float));
    }
    hulls->SetCustomData(low, high, TConstArrayView<float>(data.GetData() + low * PLUME_FLOATS,
        (high - low + 1) * PLUME_FLOATS), false);
}

//////////////////////////////////////////////////////////////////////////
// Reads the plume floats the component drawing a CPU ship holds (its pool mesh, else its hull instance)
bool ASOLNpcVisuals::GetPushedPlume(const int32 cpuShipIndex, FSOLPlumeInstance& outPlume) const
{
    namespace P = SOL::ThrusterMaterialParams;
    if (Npcs == nullptr || !Npcs->GetCpuShips().IsValidIndex(cpuShipIndex) || !mShipSlots.IsValidIndex(cpuShipIndex))
    {
        return false;
    }
    const FSOLNpcCpuShip& ship = Npcs->GetCpuShips()[cpuShipIndex];
    if (ship.bHidden != 0 || ship.RenderTier == static_cast<uint8>(SOLNpcScale::ERenderTier::Point))
    {
        return false;
    }
    const float* data = nullptr;
    const int32 poolSlot = ship.RenderTier == static_cast<uint8>(SOLNpcScale::ERenderTier::Mesh)
        ? mMeshPoolOwners.IndexOfByKey(ship.NpcIndex) : INDEX_NONE;
    if (poolSlot != INDEX_NONE)
    {
        const TArray<float>& primitive = MeshPool[poolSlot]->GetCustomPrimitiveData().Data;
        data = primitive.Num() >= PLUME_FLOATS ? primitive.GetData() : nullptr;
    }
    else
    {
        const UInstancedStaticMeshComponent* hulls = (ship.Faction & 1) == 0 ? HullsFaction0.Get()
            : HullsFaction1.Get();
        const int32 slot = mShipSlots[cpuShipIndex];
        data = slot != INDEX_NONE && (slot + 1) * PLUME_FLOATS <= hulls->PerInstanceSMCustomData.Num()
            ? hulls->PerInstanceSMCustomData.GetData() + slot * PLUME_FLOATS : nullptr;
    }
    if (data == nullptr)
    {
        return false;
    }
    outPlume.Colour = data[P::CUSTOM_COLOUR];
    outPlume.Brightness = data[P::CUSTOM_BRIGHTNESS];
    outPlume.LengthM = data[P::CUSTOM_PLUME_LENGTH_M];
    return true;
}
