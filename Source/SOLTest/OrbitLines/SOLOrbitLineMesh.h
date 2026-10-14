/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "Components.h"

#include "SOLConstants.h"

class UStaticMesh;

// Packing of orbit-line geometry and per-line parameters for M_SOLOrbitLine (SDD 17 4.2). Every vertex position is
// zero; the material rebuilds the ribbon from the full-precision UV channels:
//   UV0 = point high (x, y), UV1 = (point high z, point low x), UV2 = point low (y, z), UV3 = (dash phase, side +-1),
// with the point parent-relative in metres (J2000 ecliptic), and the unit orbit tangent (Unreal axes) in the normal.
namespace SOLOrbitLineMesh
{
    // UV channels the line mesh uses
    inline constexpr int32 UV_CHANNEL_COUNT = 4;

    // Packs one line's custom primitive data: parent - viewpoint as a double-float pair, the semi-major axis, the
    // highlight flag and the color (layout in SOL::ORBIT_LINE_CUSTOM_DATA_FLOATS)
    SOLTEST_API void PackCustomData(const FVector3d& parentMinusViewpointM, double semiMajorAxisM, bool bHighlighted,
        const FLinearColor& color, TArrayView<float> outData);

    // Resets and fills the ribbon of a sampled orbit (SOLOrbitLineMath::SampleOrbit output): two vertices per point
    // (side -1 then +1) and two triangles per segment; returns false (arrays reset) for fewer than two points,
    // mismatched arrays or more vertices than a 16-bit index buffer holds
    SOLTEST_API bool BuildRibbon(TConstArrayView<FVector3d> pointsM, TConstArrayView<FVector3d> tangents,
        TConstArrayView<double> arcLengthsM, int32 dashCount, TArray<FStaticMeshBuildVertex>& outVertices,
        TArray<uint32>& outIndices);

    // Replaces the mesh's render data with one LOD holding the ribbon (full-precision UVs, 16-bit indices, no ray
    // tracing or collision) and initializes its render resources; components using the mesh are re-created
    SOLTEST_API void ApplyRibbonToMesh(UStaticMesh& mesh, const TArray<FStaticMeshBuildVertex>& vertices,
        const TArray<uint32>& indices);
}
