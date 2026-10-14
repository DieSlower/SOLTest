/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "OrbitLines/SOLOrbitLineMesh.h"

#include "OrbitLines/SOLOrbitLineMath.h"
#include "Universe/SOLRenderPlacement.h"

#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"

namespace SOLOrbitLineMesh
{
    //////////////////////////////////////////////////////////////////////////
    // Packs one line's custom primitive data (double-float parent offset, semi-major axis, highlight, color)
    void PackCustomData(const FVector3d& parentMinusViewpointM, const double semiMajorAxisM, const bool bHighlighted,
        const FLinearColor& color, TArrayView<float> outData)
    {
        check(outData.Num() >= SOL::ORBIT_LINE_CUSTOM_DATA_FLOATS);
        FVector3f high;
        FVector3f low;
        SOLOrbitLineMath::SplitVector(parentMinusViewpointM, high, low);
        outData[0] = high.X;
        outData[1] = high.Y;
        outData[2] = high.Z;
        outData[3] = static_cast<float>(semiMajorAxisM);
        outData[4] = low.X;
        outData[5] = low.Y;
        outData[6] = low.Z;
        outData[7] = bHighlighted ? 1.0f : 0.0f;
        outData[8] = color.R;
        outData[9] = color.G;
        outData[10] = color.B;
        outData[11] = color.A;
    }

    //////////////////////////////////////////////////////////////////////////
    // Resets and fills the ribbon vertices and indices of a sampled orbit
    bool BuildRibbon(const TConstArrayView<FVector3d> pointsM, const TConstArrayView<FVector3d> tangents,
        const TConstArrayView<double> arcLengthsM, const int32 dashCount, TArray<FStaticMeshBuildVertex>& outVertices,
        TArray<uint32>& outIndices)
    {
        outVertices.Reset();
        outIndices.Reset();
        const int32 pointCount = pointsM.Num();
        if (pointCount < 2 || tangents.Num() != pointCount || arcLengthsM.Num() != pointCount
            || 2 * pointCount > static_cast<int32>(TNumericLimits<uint16>::Max()) + 1)
        {
            return false;
        }
        outVertices.Reserve(2 * pointCount);
        outIndices.Reserve(6 * (pointCount - 1));

        // Two vertices per point: the same packed point, dash phase and tangent, sides -1 and +1
        const double perimeterM = arcLengthsM[pointCount - 1];
        for (int32 index = 0; index < pointCount; ++index)
        {
            FVector3f high;
            FVector3f low;
            SOLOrbitLineMath::SplitVector(pointsM[index], high, low);
            const float dashPhase = static_cast<float>(SOLOrbitLineMath::DashPhase(arcLengthsM[index], perimeterM,
                dashCount));
            const FVector3f tangentUnreal = FVector3f(SOLRender::EclipticToUnreal(tangents[index]));
            const FVector3f perpendicular = (FMath::Abs(tangentUnreal.Z) < 0.9f ? FVector3f::UnitZ()
                : FVector3f::UnitX()).Cross(tangentUnreal).GetSafeNormal();
            for (int32 side = 0; side < 2; ++side)
            {
                FStaticMeshBuildVertex& vertex = outVertices.AddZeroed_GetRef();
                vertex.TangentZ = tangentUnreal;
                vertex.TangentX = perpendicular;
                vertex.TangentY = tangentUnreal.Cross(perpendicular);
                vertex.UVs[0] = FVector2f(high.X, high.Y);
                vertex.UVs[1] = FVector2f(high.Z, low.X);
                vertex.UVs[2] = FVector2f(low.Y, low.Z);
                vertex.UVs[3] = FVector2f(dashPhase, side == 0 ? -1.0f : 1.0f);
                vertex.Color = FColor::White;
            }
        }

        // Two triangles per segment (the material is two-sided, so the winding does not matter)
        for (uint32 index = 0; index + 1 < static_cast<uint32>(pointCount); ++index)
        {
            const uint32 first = 2 * index;
            outIndices.Append({ first, first + 2, first + 1, first + 1, first + 2, first + 3 });
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Replaces the mesh's render data with one LOD holding the ribbon and initializes its render resources
    void ApplyRibbonToMesh(UStaticMesh& mesh, const TArray<FStaticMeshBuildVertex>& vertices,
        const TArray<uint32>& indices)
    {
        // A rebuild re-creates the render state of every component using the mesh
        TOptional<FStaticMeshComponentRecreateRenderStateContext> recreateContext;
        if (mesh.AreRenderingResourcesInitialized())
        {
            recreateContext.Emplace(&mesh, false, false);
            mesh.ReleaseResources();
            mesh.ReleaseResourcesFence.Wait();
        }
        mesh.NeverStream = true;
        mesh.SetRenderData(MakeUnique<FStaticMeshRenderData>());
        FStaticMeshRenderData* renderData = mesh.GetRenderData();
        renderData->AllocateLODResources(1);
        FStaticMeshLODResources& lod = renderData->LODResources[0];

        // Full-precision UVs keep the double-float points exact; the tangent only steers the ribbon's side direction,
        // so the 8-bit tangent basis is enough, and there is no colour stream (the vertex factory binds white). No CPU
        // copies are kept
        lod.VertexBuffers.StaticMeshVertexBuffer.SetUseFullPrecisionUVs(true);
        lod.VertexBuffers.StaticMeshVertexBuffer.SetUseHighPrecisionTangentBasis(false);
        FStaticMeshVertexBufferFlags flags;
        flags.bNeedsCPUAccess = false;
        flags.bUseBackwardsCompatibleF16TruncUVs = false;
        lod.VertexBuffers.PositionVertexBuffer.Init(vertices, false);
        lod.VertexBuffers.StaticMeshVertexBuffer.Init(vertices, UV_CHANNEL_COUNT, flags);
        lod.bHasColorVertexData = false;
        lod.IndexBuffer.SetIndices(indices, EIndexBufferStride::Force16Bit);

        FStaticMeshSection& section = lod.Sections.AddDefaulted_GetRef();
        section.MaterialIndex = 0;
        section.FirstIndex = 0;
        section.NumTriangles = indices.Num() / 3;
        section.MinVertexIndex = 0;
        section.MaxVertexIndex = FMath::Max(vertices.Num() - 1, 0);
        section.bEnableCollision = false;
        section.bCastShadow = false;

        // The vertices all sit at zero; USOLOrbitLineComponent supplies the real (huge) bounds
        renderData->Bounds = FBoxSphereBounds(FVector::ZeroVector, FVector(1.0), 1.0);
        renderData->ScreenSize[0].Default = 1.0f;
        mesh.InitResources();
        mesh.CalculateExtendedBounds();
    }
}
