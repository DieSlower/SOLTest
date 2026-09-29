/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "Universe/SOLKepler.h"

namespace SOLOrbitLines
{
    constexpr int32 EllipseSegments = 90;

    // Appends EllipseSegments+1 parent-relative points (closed loop), true anomaly uniform over [0, 2*PI]
    SOLTEST_API void SampleEllipse(const FSOLKeplerElements& elements, TArray<FVector3d>& outPoints);
}
