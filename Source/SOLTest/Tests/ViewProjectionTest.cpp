/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Camera/CameraTypes.h"
#include "Engine/LocalPlayer.h"
#include "SceneView.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns the engine's projection scales (X: 1 / tan of the horizontal half FOV, Y: the vertical one) for a camera
    // FOV and a viewport, under the local player's configured aspect-ratio axis constraint
    FVector2D ViewProjectionScales(const float fovDeg, const FIntRect& viewRect)
    {
        FMinimalViewInfo view;
        view.FOV = fovDeg;
        view.AspectRatio = 16.0f / 9.0f;      // UCameraComponent's default aspect ratio
        view.bConstrainAspectRatio = false;
        FSceneViewProjectionData projection;
        projection.SetViewRectangle(viewRect);   // the engine sizes the projection from this rectangle
        FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle(view,
            GetDefault<ULocalPlayer>()->AspectRatioAxisConstraint, viewRect, projection);
        return FVector2D(projection.ProjectionMatrix.M[0][0], projection.ProjectionMatrix.M[1][1]);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLViewProjectionHorizontalFovTest,
    "SOLTest.ViewProjection.FovIsHorizontalAtEveryAspect",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Issue #25: the HUD, the map icons and picking project with the camera FOV as horizontal across the viewport, so the
// renderer must do the same at every aspect ratio (Config/DefaultEngine.ini: the local player keeps the X FOV)
bool FSOLViewProjectionHorizontalFovTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("Local player keeps the horizontal FOV"),
        static_cast<int32>(GetDefault<ULocalPlayer>()->AspectRatioAxisConstraint.GetValue()),
        static_cast<int32>(AspectRatio_MaintainXFOV));

    // The map camera's 60 deg and the chase camera's 90 deg, at 16:9, 5:4, 16:10, 21:9 and a tall viewport
    const float fovsDeg[] = { 60.0f, 90.0f };
    const FIntRect viewRects[] = { FIntRect(0, 0, 1280, 720), FIntRect(0, 0, 1280, 1024), FIntRect(0, 0, 1920, 1200),
        FIntRect(0, 0, 3440, 1440), FIntRect(0, 0, 800, 1200) };
    for (const float fovDeg : fovsDeg)
    {
        const double expected = 1.0 / FMath::Tan(FMath::DegreesToRadians(0.5 * static_cast<double>(fovDeg)));
        for (const FIntRect& viewRect : viewRects)
        {
            // The HUD's pinhole: focal = (W / 2) / tan(FOV / 2) for both axes, i.e. Y scale = X scale * W / H
            const FVector2D scales = ViewProjectionScales(fovDeg, viewRect);
            const double widthOverHeight = static_cast<double>(viewRect.Width()) / viewRect.Height();
            TestEqual(FString::Printf(TEXT("%.0f deg horizontal across %d x %d"), fovDeg, viewRect.Width(),
                viewRect.Height()), scales.X, expected, 1.0e-5);
            TestEqual(FString::Printf(TEXT("%.0f deg vertical scale at %d x %d"), fovDeg, viewRect.Width(),
                viewRect.Height()), scales.Y, expected * widthOverHeight, 1.0e-4);
        }
    }
    return true;
}

#endif
