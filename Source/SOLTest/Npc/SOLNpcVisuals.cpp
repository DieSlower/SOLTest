/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcVisuals.h"

#include "Npc/SOLNpcHullInstances.h"
#include "Npc/SOLNpcRail.h"
#include "Npc/SOLNpcScaleMath.h"
#include "Npc/SOLNpcSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLRenderPlacement.h"

#include "Async/ParallelFor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

namespace
{
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
    // Returns the placeholder hull transform of a ship (faction 0 cone along its heading, faction 1 flat box)
    FTransform HullTransform(const FVector& locationCm, const FVector& headingUnreal, int32 faction, double lengthCm)
    {
        const FVector heading = headingUnreal.IsNearlyZero() ? FVector::ForwardVector : headingUnreal.GetSafeNormal();
        if (faction == 0)
        {
            const FQuat rotation = FQuat::FindBetweenNormals(FVector::UpVector, heading);
            return FTransform(rotation, locationCm, FVector(0.4, 0.4, 1.0) * (lengthCm / BASIC_SHAPE_SIZE_CM));
        }
        const FQuat rotation = FQuat::FindBetweenNormals(FVector::ForwardVector, heading);
        return FTransform(rotation, locationCm, FVector(1.0, 0.5, 0.18) * (lengthCm / BASIC_SHAPE_SIZE_CM));
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
    UStaticMesh* hullMeshes[2] = {
        LoadObject<UStaticMesh>(nullptr, SOL::Paths::NPC_HULL_MESH_FACTION_0),
        LoadObject<UStaticMesh>(nullptr, SOL::Paths::NPC_HULL_MESH_FACTION_1),
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
    GroupTexture = CreateFloatTexture(SOL::NPC_GROUP_TEXTURE_WIDTH, 2);
    FarMaterial = UMaterialInstanceDynamic::Create(pointMaterial, this);
    CpuPointMaterial = UMaterialInstanceDynamic::Create(pointMaterial, this);
    for (UMaterialInstanceDynamic* material : { FarMaterial.Get(), CpuPointMaterial.Get() })
    {
        material->SetTextureParameterValue(PARAM_GROUP_TEX, GroupTexture);
        material->SetVectorParameterValue(PARAM_FACTION_0, SOL::NPC_FACTION_COLORS[0]);
        material->SetVectorParameterValue(PARAM_FACTION_1, SOL::NPC_FACTION_COLORS[1]);
    }
    FarPoints->SetStaticMesh(PointBlockMesh);
    FarPoints->SetMaterial(0, FarMaterial);
    CpuPoints->SetStaticMesh(PointBlockMesh);
    CpuPoints->SetMaterial(0, CpuPointMaterial);

    // Hull layers: one ISM per faction, and the closest-ship mesh pool
    UInstancedStaticMeshComponent* hulls[2] = { HullsFaction0, HullsFaction1 };
    for (int32 faction = 0; faction < 2; ++faction)
    {
        UMaterialInstanceDynamic* tint = UMaterialInstanceDynamic::Create(hullMaterial, this);
        tint->SetVectorParameterValue(PARAM_HULL_TINT, SOL::NPC_FACTION_COLORS[faction]);
        hulls[faction]->SetStaticMesh(hullMeshes[faction]);
        hulls[faction]->SetMaterial(0, tint);
        hulls[faction]->SetHasPerInstancePrevTransforms(true);
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
    Super::EndPlay(endPlayReason);
}

//////////////////////////////////////////////////////////////////////////
// Redraws every tier for this frame; bound to OnNpcsUpdated
void ASOLNpcVisuals::Redraw()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLNpcVisuals::Redraw);
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
    UpdateFarPoints();
    UpdateCpuShips();
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
    mUploadedRevision = Npcs->GetPopulationRevision();
    const int32 shipCount = Npcs->GetRails().Num();
    if (shipCount == 0)
    {
        // No population: release the rail texture (tens of MB at 1M) and draw no far blocks
        RailTexture = nullptr;
        FarMaterial->SetTextureParameterValue(PARAM_RAIL_TEX, nullptr);
        CpuPointMaterial->SetTextureParameterValue(PARAM_RAIL_TEX, nullptr);
        SizePointBlocks(FarPoints, 0, mFarBlockCount);
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
    FillRailTexels(texels, width * height, mGpuRailEpochS);
    mip.BulkData.Unlock();
    RailTexture->UpdateResource();
    FarMaterial->SetTextureParameterValue(PARAM_RAIL_TEX, RailTexture);
    CpuPointMaterial->SetTextureParameterValue(PARAM_RAIL_TEX, RailTexture);
    SizePointBlocks(FarPoints, FMath::DivideAndRoundUp(shipCount, SOL::NPC_POINT_BLOCK_SHIPS), mFarBlockCount);
    UE_LOG(LogSOL, Log, TEXT("NpcVisuals %s: uploaded %d rails (%dx%d texels, %.1f MB), %d far blocks"), *GetName(),
        shipCount, width, height, width * height * sizeof(FVector4f) / (1024.0 * 1024.0), mFarBlockCount);
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
    FillRailTexels(texels, width * height, mGpuRailEpochS);
    FUpdateTextureRegion2D* region = new FUpdateTextureRegion2D(0, 0, 0, 0, width, height);
    RailTexture->UpdateTextureRegions(0, 1, region, width * sizeof(FVector4f), sizeof(FVector4f),
        reinterpret_cast<uint8*>(texels), [](uint8* data, const FUpdateTextureRegion2D* regions)
        {
            delete[] reinterpret_cast<FVector4f*>(data);
            delete regions;
        });
}

//////////////////////////////////////////////////////////////////////////
// Fills an upload buffer with the population's rails re-based to epochRailTimeS (two texels per ship)
void ASOLNpcVisuals::FillRailTexels(FVector4f* texels, const int32 texelCount, const double epochRailTimeS) const
{
    // Two texels per ship: (kind + 4 faction + 8 group, P0, P1, P2), (P3, P4, P5, P6); the padding is zero
    const TConstArrayView<FSOLNpcRail> rails = Npcs->GetRails();
    const int32 shipCount = FMath::Min(rails.Num(), texelCount / 2);
    constexpr int32 SHIPS_PER_TASK = 16384;
    ParallelFor(FMath::DivideAndRoundUp(shipCount, SHIPS_PER_TASK), [&](const int32 block)
    {
        const int32 end = FMath::Min((block + 1) * SHIPS_PER_TASK, shipCount);
        for (int32 ship = block * SHIPS_PER_TASK; ship < end; ++ship)
        {
            const FSOLNpcRail rail = SOLNpcRail::AdvancedToEpoch(rails[ship], epochRailTimeS);
            const float packed = static_cast<float>(rail.Kind + 4 * rail.Faction + 8 * rail.Group);
            texels[ship * 2] = FVector4f(packed, rail.P[0], rail.P[1], rail.P[2]);
            texels[ship * 2 + 1] = FVector4f(rail.P[3], rail.P[4], rail.P[5], rail.P[6]);
        }
    });
    if (shipCount * 2 < texelCount)
    {
        FMemory::Memzero(texels + shipCount * 2, static_cast<SIZE_T>(texelCount - shipCount * 2) * sizeof(FVector4f));
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
    texels.SetNumZeroed(SOL::NPC_GROUP_TEXTURE_WIDTH * 2, EAllowShrinking::No);
    for (int32 group = 0; group < groupCount; ++group)
    {
        texels[group] = FVector4f(FVector3f(frame.GroupPointAM[group] - viewpointM), 0.0f);
        texels[SOL::NPC_GROUP_TEXTURE_WIDTH + group] = FVector4f(FVector3f(frame.GroupPointBM[group] - viewpointM),
            0.0f);
    }
    mGroupRegions[mUploadRing] = FUpdateTextureRegion2D(0, 0, 0, 0, SOL::NPC_GROUP_TEXTURE_WIDTH, 2);
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
    const float gpuHideRadiusM = static_cast<float>(Npcs->GetStats().MidRadiusM);
    int32 meshSlots[2] = { 0, 1 };

    // Returns a ship's render location, previous location and hull heading (steering heading when the AI flies it,
    // else the motion since last frame)
    const auto placeShip = [&origin](const FSOLNpcCpuShip& ship, FVector& outLocationCm, FVector& outPrevLocationCm,
        FVector& outHeading)
    {
        outLocationCm = FVector(origin.UniverseToRenderCm(ship.PositionM));
        outPrevLocationCm = FVector(origin.UniverseToRenderCm(ship.PrevPositionM));
        outHeading = ship.HeadingDirection.IsZero() ? outLocationCm - outPrevLocationCm
            : FVector(origin.UniverseToRenderCm(ship.PositionM + FVector3d(ship.HeadingDirection)
                * SOL::NPC_HULL_LENGTH_M)) - outLocationCm;
    };

    // Serial pass: each ship's slot in its layer (texel or per-faction instance; mesh-pool ships are placed here,
    // INDEX_NONE), so the heavy per-ship work below can run in parallel (SDD 11 Amendment 6)
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLNpcVisuals::UpdateCpuShips);
    mShipSlots.SetNumUninitialized(ships.Num(), EAllowShrinking::No);
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
            // here too would double it
            mShipSlots[index] = ship.RailDistanceM >= gpuHideRadiusM ? INDEX_NONE : pointCount++;
            continue;
        }
        if (ship.RenderTier == static_cast<uint8>(SOLNpcScale::ERenderTier::Mesh)
            && meshSlots[faction] < NPC_MESH_POOL_SIZE)
        {
            FVector locationCm, prevLocationCm, heading;
            placeShip(ship, locationCm, prevLocationCm, heading);
            const int32 poolSlot = meshSlots[faction];
            UStaticMeshComponent* mesh = MeshPool[poolSlot];
            meshSlots[faction] += 2;
            mesh->SetWorldTransform(HullTransform(locationCm, heading, faction, lengthCm));
            if (mMeshPoolOwners[poolSlot] != ship.NpcIndex)
            {
                // A new owner: no motion vector from where the previous ship was
                mesh->ResetSceneVelocity();
                mMeshPoolOwners[poolSlot] = ship.NpcIndex;
            }
            mesh->SetVisibility(true);
            mShipSlots[index] = INDEX_NONE;
            continue;
        }
        mShipSlots[index] = instanceCounts[faction]++;
    }
    cpuTexels.SetNumUninitialized(pointCount, EAllowShrinking::No);
    for (int32 faction = 0; faction < 2; ++faction)
    {
        mHullTransforms[faction].SetNumUninitialized(instanceCounts[faction], EAllowShrinking::No);
        mHullPrevTransforms[faction].SetNumUninitialized(instanceCounts[faction], EAllowShrinking::No);
    }

    // Parallel pass: point texels and instanced hulls (never smaller than a few pixels, previous transform from the
    // ship's own last position)
    constexpr int32 SHIPS_PER_TASK = 1024;
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLNpcVisuals::PlaceShips);
    ParallelFor(FMath::DivideAndRoundUp(ships.Num(), SHIPS_PER_TASK), [&](const int32 block)
    {
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
                cpuTexels[slot] = FVector4f(FVector3f(ship.PositionM - viewpointM), static_cast<float>(faction));
                continue;
            }
            FVector locationCm, prevLocationCm, heading;
            placeShip(ship, locationCm, prevLocationCm, heading);
            const double distanceCm = FMath::Max((locationCm - viewRenderCm).Size(), 1.0);
            const double sizeCm = FMath::Max(lengthCm, SOL::NPC_HULL_MIN_PIXELS * pixelPerCm * distanceCm);
            mHullTransforms[faction][slot] = HullTransform(locationCm, heading, faction, sizeCm);
            mHullPrevTransforms[faction][slot] = HullTransform(prevLocationCm, heading, faction, sizeCm);
        }
    });

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
    double fovDeg = 90.0;
    double viewportWidth = 1920.0;
    const APlayerController* controller = GetWorld()->GetFirstPlayerController();
    if (controller != nullptr && controller->PlayerCameraManager != nullptr)
    {
        fovDeg = controller->PlayerCameraManager->GetFOVAngle();
    }
    if (GetWorld()->GetGameViewport() != nullptr)
    {
        FVector2D size;
        GetWorld()->GetGameViewport()->GetViewportSize(size);
        viewportWidth = size.X > 0.0 ? size.X : viewportWidth;
    }

    // The camera's FOV is horizontal, so one pixel spans the view width over the viewport's pixel width (square
    // pixels, any aspect)
    return 2.0 * distanceCm * FMath::Tan(FMath::DegreesToRadians(fovDeg) * 0.5) / viewportWidth;
}
