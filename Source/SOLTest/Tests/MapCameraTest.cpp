/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLMapCamera.h"

#include "Tests/SOLTestHelpers.h"
#include "Universe/SOLRenderPlacement.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the jump-map orbit-camera contract (Docs/SDDs/3-jump-map.md, Appendix E + Amendment 1). Universe/ecliptic
// frame, meters; Up = ecliptic north (0,0,1). Positive Pitch places the camera above the ecliptic (offset toward +Up).
namespace
{
    const FVector3d MAP_CAM_UP(0.0, 0.0, 1.0);

    // Relative tolerance for pure trigonometric results
    constexpr double MAP_CAM_REL_TOL = 1.0e-12;

    // Unit-vector tolerance for direction comparisons
    constexpr double MAP_CAM_DIR_TOL = 1.0e-9;

    // Absolute tolerance (meters) for offsets taken off a ~1e11 m focus (double ulp there is ~1.5e-5 m)
    constexpr double MAP_CAM_LARGE_ABS_TOL_M = 1.0e-3;

    //////////////////////////////////////////////////////////////////////////
    // Builds an orbit-camera state from its four fields
    FSOLOrbitCameraState MapCamMakeState(const FVector3d& focusM, const double yawRad, const double pitchRad,
        const double distanceM)
    {
        FSOLOrbitCameraState state;
        state.FocusPositionM = focusM;
        state.YawRad = yawRad;
        state.PitchRad = pitchRad;
        state.DistanceM = distanceM;
        return state;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the horizontal (XY) part of a vector
    FVector3d MapCamHorizontal(const FVector3d& value)
    {
        return FVector3d(value.X, value.Y, 0.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two orbit-camera states are bit-for-bit equal in every field
    bool MapCamStatesIdentical(const FSOLOrbitCameraState& a, const FSOLOrbitCameraState& b)
    {
        return a.FocusPositionM == b.FocusPositionM && a.YawRad == b.YawRad && a.PitchRad == b.PitchRad
            && a.DistanceM == b.DistanceM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the pan displacement of the focus for the given pan deltas (difference first, precision-safe)
    FVector3d MapCamPanDisplacement(const FSOLOrbitCameraState& state, const double rightM, const double upM)
    {
        return SOLMapCamera::ApplyPan(state, rightM, upM).FocusPositionM - state.FocusPositionM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the expected ecliptic "right" for a state: horizontal look direction crossed with Up (independent of the
    // yaw sign convention; derived from the camera position the contract defines)
    FVector3d MapCamExpectedRight(const FSOLOrbitCameraState& state)
    {
        const FVector3d lookH = MapCamHorizontal(state.FocusPositionM - SOLMapCamera::ComputeCameraPositionM(state));
        return FVector3d::CrossProduct(lookH.GetSafeNormal(), MAP_CAM_UP);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the expected ecliptic "forward-horizontal" for a state: the normalized horizontal look direction
    FVector3d MapCamExpectedForwardH(const FSOLOrbitCameraState& state)
    {
        return MapCamHorizontal(state.FocusPositionM - SOLMapCamera::ComputeCameraPositionM(state)).GetSafeNormal();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the closed-form pan right at a yaw: (0,-1,0) rotated around Up by yaw = (sin yaw, -cos yaw, 0)
    FVector3d MapCamRightFromYaw(const double yawRad)
    {
        return FVector3d(FMath::Sin(yawRad), -FMath::Cos(yawRad), 0.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the closed-form pan forward-horizontal at a yaw: (1,0,0) rotated around Up by yaw = (cos yaw, sin yaw, 0)
    FVector3d MapCamForwardHFromYaw(const double yawRad)
    {
        return FVector3d(FMath::Cos(yawRad), FMath::Sin(yawRad), 0.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks the orientation's forward/up/right axes against the look-at definition built from the camera position
    void MapCamCheckOrientation(FAutomationTestBase& test, const FSOLOrbitCameraState& state, const FString& label)
    {
        const FVector3d cameraM = SOLMapCamera::ComputeCameraPositionM(state);
        const FVector3d toFocus = state.FocusPositionM - cameraM;
        test.TestTrue(label + TEXT(": camera is DistanceM from the focus"),
            FMath::Abs(toFocus.Size() - state.DistanceM) <= MAP_CAM_REL_TOL * 10.0 * state.DistanceM);

        const FVector3d forwardEcl = toFocus.GetSafeNormal();
        const FVector3d upEcl =
            (MAP_CAM_UP - FVector3d::DotProduct(MAP_CAM_UP, forwardEcl) * forwardEcl).GetSafeNormal();
        const FVector3d rightEcl = FVector3d::CrossProduct(forwardEcl, upEcl);

        const FQuat4d rotation = SOLMapCamera::ComputeCameraOrientation(state);
        test.TestTrue(label + TEXT(": quaternion is normalized"),
            FMath::Abs(rotation.Size() - 1.0) <= 1.0e-9);

        const FVector3d forwardUe = rotation.RotateVector(FVector3d(1.0, 0.0, 0.0));
        const FVector3d rightUe = rotation.RotateVector(FVector3d(0.0, 1.0, 0.0));
        const FVector3d upUe = rotation.RotateVector(FVector3d(0.0, 0.0, 1.0));
        test.TestTrue(label + TEXT(": forward points at the focus (Unreal axes)"),
            forwardUe.Equals(SOLRender::EclipticToUnreal(forwardEcl), MAP_CAM_DIR_TOL));
        test.TestTrue(label + TEXT(": up is world Up re-orthogonalized against forward (Unreal axes)"),
            upUe.Equals(SOLRender::EclipticToUnreal(upEcl), MAP_CAM_DIR_TOL));
        test.TestTrue(label + TEXT(": right is forward x up in ecliptic, mirrored to Unreal"),
            rightUe.Equals(SOLRender::EclipticToUnreal(rightEcl), MAP_CAM_DIR_TOL));
    }
}

// --- ComputeCameraPositionM ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraPositionZeroAnglesTest,
    "SOLTest.MapCamera.PositionZeroAnglesBehindFocus",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At Yaw=0, Pitch=0 the camera sits DistanceM along -EclipticX from the focus
bool FSOLMapCameraPositionZeroAnglesTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d atOrigin =
        SOLMapCamera::ComputeCameraPositionM(MapCamMakeState(FVector3d::ZeroVector, 0.0, 0.0, 5.0));
    TestTrue(TEXT("Camera at (-5, 0, 0)"), atOrigin.Equals(FVector3d(-5.0, 0.0, 0.0), 1.0e-12));

    const FVector3d focus(10.0, -20.0, 30.0);
    const FVector3d offset = SOLMapCamera::ComputeCameraPositionM(MapCamMakeState(focus, 0.0, 0.0, 1.0e10)) - focus;
    TestTrue(TEXT("Offset is (-1e10, 0, 0)"), offset.Equals(FVector3d(-1.0e10, 0.0, 0.0), 1.0e-3));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraPositionYawQuarterTest, "SOLTest.MapCamera.PositionYawRotatesAroundUp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Yaw rotates the (-D,0,0) offset around Up: 90 deg lands on the Y axis, 180 deg on +X, +/-yaw mirror across X
bool FSOLMapCameraPositionYawQuarterTest::RunTest(const FString& /*parameters*/)
{
    const double distanceM = 1000.0;
    const FVector3d quarter = SOLMapCamera::ComputeCameraPositionM(
        MapCamMakeState(FVector3d::ZeroVector, UE_DOUBLE_HALF_PI, 0.0, distanceM));
    TestTrue(TEXT("90 deg yaw: X is ~0"), FMath::Abs(quarter.X) <= 1.0e-9);
    TestTrue(TEXT("90 deg yaw: |Y| is D"), FMath::Abs(FMath::Abs(quarter.Y) - distanceM) <= 1.0e-9);
    TestTrue(TEXT("90 deg yaw: Z is 0"), FMath::Abs(quarter.Z) <= 1.0e-9);

    const FVector3d half = SOLMapCamera::ComputeCameraPositionM(
        MapCamMakeState(FVector3d::ZeroVector, UE_DOUBLE_PI, 0.0, distanceM));
    TestTrue(TEXT("180 deg yaw: camera at (+D, 0, 0)"), half.Equals(FVector3d(distanceM, 0.0, 0.0), 1.0e-9));

    // +yaw and -yaw are mirror images across the X axis whatever the rotation sense
    const FVector3d plus =
        SOLMapCamera::ComputeCameraPositionM(MapCamMakeState(FVector3d::ZeroVector, 0.7, 0.0, distanceM));
    const FVector3d minus =
        SOLMapCamera::ComputeCameraPositionM(MapCamMakeState(FVector3d::ZeroVector, -0.7, 0.0, distanceM));
    TestTrue(TEXT("+/-yaw share X"), FMath::Abs(plus.X - minus.X) <= 1.0e-9);
    TestTrue(TEXT("+/-yaw have opposite Y"), FMath::Abs(plus.Y + minus.Y) <= 1.0e-9);
    TestTrue(TEXT("0.7 rad yaw: X = -D cos(0.7)"), FMath::Abs(plus.X + distanceM * FMath::Cos(0.7)) <= 1.0e-9);
    TestTrue(TEXT("0.7 rad yaw: |Y| = D sin(0.7)"),
        FMath::Abs(FMath::Abs(plus.Y) - distanceM * FMath::Sin(0.7)) <= 1.0e-9);

    // Consecutive quarter turns keep the same rotation sense (yaw 90 then yaw 180 then yaw -90 go round the circle)
    const FVector3d negQuarter = SOLMapCamera::ComputeCameraPositionM(
        MapCamMakeState(FVector3d::ZeroVector, -UE_DOUBLE_HALF_PI, 0.0, distanceM));
    TestTrue(TEXT("-90 deg yaw is opposite +90 deg yaw"), negQuarter.Equals(-quarter, 1.0e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraOrbitYawQuarterTurnSignTest, "SOLTest.MapCamera.OrbitYawQuarterTurnSign",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Pins the yaw sign (right-hand rule around Up): Yaw=+PI/2 turns the (-D,0,0) offset into (0,-D,0), not (0,+D,0)
bool FSOLMapCameraOrbitYawQuarterTurnSignTest::RunTest(const FString& /*parameters*/)
{
    const double distanceM = 1000.0;
    const FVector3d atZero = SOLMapCamera::ComputeCameraPositionM(
        MapCamMakeState(FVector3d::ZeroVector, 0.0, 0.0, distanceM));
    TestTrue(TEXT("Yaw 0 offset is (-D, 0, 0)"), atZero.Equals(FVector3d(-distanceM, 0.0, 0.0), 1.0e-9));

    const FVector3d quarter = SOLMapCamera::ComputeCameraPositionM(
        MapCamMakeState(FVector3d::ZeroVector, UE_DOUBLE_HALF_PI, 0.0, distanceM));
    TestTrue(TEXT("Yaw +PI/2 offset is (0, -D, 0)"), quarter.Equals(FVector3d(0.0, -distanceM, 0.0), 1.0e-9));
    TestTrue(TEXT("Yaw +PI/2 offset Y is negative (sign pinned)"), quarter.Y < 0.0);

    // Same sign through ApplyOrbitDelta: a +PI/2 yaw delta from Yaw=0 lands the camera on -Y too
    const FSOLOrbitCameraParams params;
    const FVector3d focus(4.0e9, -2.0e9, 1.0e8);
    const FSOLOrbitCameraState turned =
        SOLMapCamera::ApplyOrbitDelta(MapCamMakeState(focus, 0.0, 0.0, 1.0e10), UE_DOUBLE_HALF_PI, 0.0, params);
    const FVector3d offset = SOLMapCamera::ComputeCameraPositionM(turned) - focus;
    TestTrue(TEXT("After a +PI/2 orbit delta the offset is (0, -1e10, 0)"),
        offset.Equals(FVector3d(0.0, -1.0e10, 0.0), 1.0e-3));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraPositionPitchTest, "SOLTest.MapCamera.PositionPitchTiltsTowardUp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Positive pitch raises the camera above the ecliptic, negative lowers it; near the clamp it is almost straight up/down
bool FSOLMapCameraPositionPitchTest::RunTest(const FString& /*parameters*/)
{
    const double distanceM = 1000.0;
    const FVector3d high =
        SOLMapCamera::ComputeCameraPositionM(MapCamMakeState(FVector3d::ZeroVector, 0.0, 1.5, distanceM));
    const FVector3d low =
        SOLMapCamera::ComputeCameraPositionM(MapCamMakeState(FVector3d::ZeroVector, 0.0, -1.5, distanceM));
    TestTrue(TEXT("Pitch +1.5: Z = D sin(1.5)"), FMath::Abs(high.Z - distanceM * FMath::Sin(1.5)) <= 1.0e-9);
    TestTrue(TEXT("Pitch +1.5: mostly along +Up"), high.GetSafeNormal().Z > 0.99);
    TestTrue(TEXT("Pitch -1.5: Z = -D sin(1.5)"), FMath::Abs(low.Z + distanceM * FMath::Sin(1.5)) <= 1.0e-9);
    TestTrue(TEXT("Pitch -1.5: mostly along -Up"), low.GetSafeNormal().Z < -0.99);

    // At Yaw=0 the horizontal part stays on -X: (-D cos p, 0, D sin p)
    const FVector3d mid =
        SOLMapCamera::ComputeCameraPositionM(MapCamMakeState(FVector3d::ZeroVector, 0.0, -0.5, distanceM));
    TestTrue(TEXT("Yaw 0, pitch -0.5: (-D cos, 0, D sin)"),
        mid.Equals(FVector3d(-distanceM * FMath::Cos(-0.5), 0.0, distanceM * FMath::Sin(-0.5)), 1.0e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraPositionDistanceTest, "SOLTest.MapCamera.PositionAlwaysAtDistance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// For every sampled yaw/pitch, the camera is exactly DistanceM from the focus, at height D sin(pitch)
bool FSOLMapCameraPositionDistanceTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d focus(3.0e8, -4.0e8, 1.0e7);
    const double yaws[] = { -3.0, -1.2, 0.0, 0.4, UE_DOUBLE_HALF_PI, 2.5, UE_DOUBLE_PI };
    const double pitches[] = { -1.5, -0.5, 0.0, 0.3, 1.0, 1.5 };
    const double distances[] = { 1.0e6, 1.0e10, 6.0e13 };
    for (const double distanceM : distances)
    {
        for (const double yawRad : yaws)
        {
            for (const double pitchRad : pitches)
            {
                const FVector3d offset = SOLMapCamera::ComputeCameraPositionM(
                    MapCamMakeState(focus, yawRad, pitchRad, distanceM)) - focus;
                const FString label = FString::Printf(TEXT("D=%g yaw=%g pitch=%g"), distanceM, yawRad, pitchRad);
                TestTrue(label + TEXT(": |offset| == D"),
                    SOLTestHelpers::RelativeError(offset.Size(), distanceM) <= 1.0e-9);
                TestTrue(label + TEXT(": height == D sin(pitch)"),
                    FMath::Abs(offset.Z - distanceM * FMath::Sin(pitchRad)) <= 1.0e-9 * distanceM);
                TestTrue(label + TEXT(": horizontal == D cos(pitch)"),
                    FMath::Abs(MapCamHorizontal(offset).Size() - distanceM * FMath::Cos(pitchRad))
                        <= 1.0e-9 * distanceM);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraPositionLargeFocusTest,
    "SOLTest.MapCamera.PositionLargeFocusOffsetsExactly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A solar-system-scale focus shifts the camera by exactly the focus: offset matches the same state at the origin
bool FSOLMapCameraPositionLargeFocusTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d bigFocus(1.5e11, -7.3e10, 2.1e9);
    const double samples[][2] = { { 0.0, 0.0 }, { 0.9, -0.5 }, { -2.2, 1.1 } };
    for (const auto& sample : samples)
    {
        const FVector3d atOrigin = SOLMapCamera::ComputeCameraPositionM(
            MapCamMakeState(FVector3d::ZeroVector, sample[0], sample[1], 25.0));
        const FVector3d atBig =
            SOLMapCamera::ComputeCameraPositionM(MapCamMakeState(bigFocus, sample[0], sample[1], 25.0));
        const FString label = FString::Printf(TEXT("yaw=%g pitch=%g"), sample[0], sample[1]);
        TestTrue(label + TEXT(": origin-focus offset has length 25"), FMath::Abs(atOrigin.Size() - 25.0) <= 1.0e-9);
        TestTrue(label + TEXT(": big-focus offset matches origin-focus offset"),
            (atBig - bigFocus).Equals(atOrigin, MAP_CAM_LARGE_ABS_TOL_M));
    }
    return true;
}

// --- ComputeCameraOrientation ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraOrientationLookAtTest, "SOLTest.MapCamera.OrientationLooksAtFocus",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The quaternion's forward points at the focus, up is world Up re-orthogonalized, right completes the Unreal frame
bool FSOLMapCameraOrientationLookAtTest::RunTest(const FString& /*parameters*/)
{
    const double yaws[] = { -2.8, -1.0, 0.0, 0.6, UE_DOUBLE_HALF_PI, 2.0, UE_DOUBLE_PI };
    const double pitches[] = { -1.2, -0.5, -0.1, 0.25, 0.9 };
    for (const double yawRad : yaws)
    {
        for (const double pitchRad : pitches)
        {
            MapCamCheckOrientation(*this, MapCamMakeState(FVector3d(1.0e9, 2.0e9, -3.0e8), yawRad, pitchRad, 1.0e10),
                FString::Printf(TEXT("yaw=%g pitch=%g"), yawRad, pitchRad));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraOrientationExplicitTest, "SOLTest.MapCamera.OrientationExplicitAxes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Hand-derived axes: at Yaw=0, pitch p the camera looks along +X tilted by -p; Unreal forward = (cos p, 0, -sin p)
bool FSOLMapCameraOrientationExplicitTest::RunTest(const FString& /*parameters*/)
{
    const double pitchRad = -0.5;
    const FQuat4d rotation = SOLMapCamera::ComputeCameraOrientation(
        MapCamMakeState(FVector3d::ZeroVector, 0.0, pitchRad, 1.0e10));
    const FVector3d forwardUe = rotation.RotateVector(FVector3d(1.0, 0.0, 0.0));
    const FVector3d rightUe = rotation.RotateVector(FVector3d(0.0, 1.0, 0.0));
    const FVector3d upUe = rotation.RotateVector(FVector3d(0.0, 0.0, 1.0));
    TestTrue(TEXT("Forward = (cos p, 0, -sin p)"),
        forwardUe.Equals(FVector3d(FMath::Cos(pitchRad), 0.0, -FMath::Sin(pitchRad)), MAP_CAM_DIR_TOL));
    TestTrue(TEXT("Up = (sin p, 0, cos p)"),
        upUe.Equals(FVector3d(FMath::Sin(pitchRad), 0.0, FMath::Cos(pitchRad)), MAP_CAM_DIR_TOL));
    TestTrue(TEXT("Right = Unreal +Y (ecliptic -Y)"), rightUe.Equals(FVector3d(0.0, 1.0, 0.0), MAP_CAM_DIR_TOL));

    // At Yaw=PI, Pitch=0 the camera is on +X looking toward -X: Unreal forward (-1,0,0), right is Unreal -Y
    const FQuat4d flipped = SOLMapCamera::ComputeCameraOrientation(
        MapCamMakeState(FVector3d::ZeroVector, UE_DOUBLE_PI, 0.0, 1.0e10));
    TestTrue(TEXT("Yaw PI: forward = (-1, 0, 0)"),
        flipped.RotateVector(FVector3d(1.0, 0.0, 0.0)).Equals(FVector3d(-1.0, 0.0, 0.0), MAP_CAM_DIR_TOL));
    TestTrue(TEXT("Yaw PI: up = (0, 0, 1)"),
        flipped.RotateVector(FVector3d(0.0, 0.0, 1.0)).Equals(FVector3d(0.0, 0.0, 1.0), MAP_CAM_DIR_TOL));
    TestTrue(TEXT("Yaw PI: right = (0, -1, 0)"),
        flipped.RotateVector(FVector3d(0.0, 1.0, 0.0)).Equals(FVector3d(0.0, -1.0, 0.0), MAP_CAM_DIR_TOL));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraOrientationNearClampTest, "SOLTest.MapCamera.OrientationAtPitchClamp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At the default pitch clamp (+/-1.5 rad) the look-at is still well defined; the exact +/-PI/2 pole is not asserted
bool FSOLMapCameraOrientationNearClampTest::RunTest(const FString& /*parameters*/)
{
    const FSOLOrbitCameraParams params;
    const FVector3d focus(-2.0e10, 5.0e9, 0.0);
    const double yaws[] = { 0.0, 1.3, -2.4 };
    for (const double yawRad : yaws)
    {
        MapCamCheckOrientation(*this, MapCamMakeState(focus, yawRad, params.MaxPitchRad, 3.0e9),
            FString::Printf(TEXT("max pitch yaw=%g"), yawRad));
        MapCamCheckOrientation(*this, MapCamMakeState(focus, yawRad, params.MinPitchRad, 3.0e9),
            FString::Printf(TEXT("min pitch yaw=%g"), yawRad));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraOrientationLargeFocusTest,
    "SOLTest.MapCamera.OrientationLargeFocusUnaffected",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The orientation depends only on yaw/pitch: a ~1e11 m focus gives the same camera axes as the origin
bool FSOLMapCameraOrientationLargeFocusTest::RunTest(const FString& /*parameters*/)
{
    const FQuat4d atOrigin =
        SOLMapCamera::ComputeCameraOrientation(MapCamMakeState(FVector3d::ZeroVector, 1.1, -0.7, 50.0));
    const FQuat4d atBig = SOLMapCamera::ComputeCameraOrientation(
        MapCamMakeState(FVector3d(1.5e11, -7.3e10, 2.1e9), 1.1, -0.7, 50.0));
    const FVector3d axes[] = { FVector3d(1.0, 0.0, 0.0), FVector3d(0.0, 1.0, 0.0), FVector3d(0.0, 0.0, 1.0) };
    for (const FVector3d& axis : axes)
    {
        TestTrue(TEXT("Axis matches between origin and large focus"),
            atBig.RotateVector(axis).Equals(atOrigin.RotateVector(axis), 1.0e-6));
    }
    MapCamCheckOrientation(*this, MapCamMakeState(FVector3d::ZeroVector, 1.1, -0.7, 50.0), TEXT("origin focus"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraOrientationContinuityTest, "SOLTest.MapCamera.OrientationContinuous",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A small yaw/pitch perturbation away from the poles changes the camera axes only slightly (no sudden flip)
bool FSOLMapCameraOrientationContinuityTest::RunTest(const FString& /*parameters*/)
{
    const double step = 1.0e-3;
    const double yaws[] = { -3.1, -1.5, 0.0, 0.8, 3.1 };
    const double pitches[] = { -1.4, -0.5, 0.0, 0.7, 1.4 };
    for (const double yawRad : yaws)
    {
        for (const double pitchRad : pitches)
        {
            const FSOLOrbitCameraState base = MapCamMakeState(FVector3d::ZeroVector, yawRad, pitchRad, 1.0e10);
            const FSOLOrbitCameraState nudged =
                MapCamMakeState(FVector3d::ZeroVector, yawRad + step, pitchRad + step, 1.0e10);
            const FQuat4d a = SOLMapCamera::ComputeCameraOrientation(base);
            const FQuat4d b = SOLMapCamera::ComputeCameraOrientation(nudged);
            const FString label = FString::Printf(TEXT("yaw=%g pitch=%g"), yawRad, pitchRad);
            TestTrue(label + TEXT(": forward changes by < 0.01"),
                (a.RotateVector(FVector3d::ForwardVector) - b.RotateVector(FVector3d::ForwardVector)).Size() < 0.01);
            TestTrue(label + TEXT(": up changes by < 0.05"),
                (a.RotateVector(FVector3d::UpVector) - b.RotateVector(FVector3d::UpVector)).Size() < 0.05);

            // The base orientation must also be the correct look-at (continuity alone is trivially met by a constant)
            MapCamCheckOrientation(*this, base, label);
        }
    }

    // Crossing the yaw wrap (PI -> -PI) is continuous too
    const FQuat4d beforeWrap = SOLMapCamera::ComputeCameraOrientation(
        MapCamMakeState(FVector3d::ZeroVector, UE_DOUBLE_PI - 1.0e-4, -0.5, 1.0e10));
    const FQuat4d afterWrap = SOLMapCamera::ComputeCameraOrientation(
        MapCamMakeState(FVector3d::ZeroVector, -UE_DOUBLE_PI + 1.0e-4, -0.5, 1.0e10));
    TestTrue(TEXT("Forward continuous across the yaw wrap"),
        (beforeWrap.RotateVector(FVector3d::ForwardVector) - afterWrap.RotateVector(FVector3d::ForwardVector)).Size()
            < 0.01);
    return true;
}

// --- ApplyOrbitDelta ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraOrbitYawAddTest, "SOLTest.MapCamera.OrbitYawAddsDelta",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A yaw delta inside the range adds exactly; focus, distance and (for zero pitch delta) pitch are untouched
bool FSOLMapCameraOrbitYawAddTest::RunTest(const FString& /*parameters*/)
{
    const FSOLOrbitCameraParams params;
    const FSOLOrbitCameraState start = MapCamMakeState(FVector3d(1.5e11, -2.0e10, 3.0e8), 0.4, -0.5, 7.0e9);
    const FSOLOrbitCameraState result = SOLMapCamera::ApplyOrbitDelta(start, 0.35, 0.0, params);
    TestTrue(TEXT("Yaw = 0.75"), FMath::Abs(result.YawRad - 0.75) <= MAP_CAM_REL_TOL);
    TestEqual(TEXT("Pitch unchanged"), result.PitchRad, start.PitchRad);
    TestTrue(TEXT("Focus unchanged"), result.FocusPositionM == start.FocusPositionM);
    TestEqual(TEXT("Distance unchanged"), result.DistanceM, start.DistanceM);

    const FSOLOrbitCameraState negative = SOLMapCamera::ApplyOrbitDelta(start, -1.0, 0.0, params);
    TestTrue(TEXT("Yaw = -0.6"), FMath::Abs(negative.YawRad + 0.6) <= MAP_CAM_REL_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraOrbitYawWrapTest, "SOLTest.MapCamera.OrbitYawWrapsToHalfOpenRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Yaw wraps into (-PI, PI]: past +PI lands near -PI, past -PI near +PI, -PI itself maps to +PI, multi-turns reduce
bool FSOLMapCameraOrbitYawWrapTest::RunTest(const FString& /*parameters*/)
{
    const FSOLOrbitCameraParams params;
    const FVector3d focus(1.0, 2.0, 3.0);

    const FSOLOrbitCameraState overPi =
        SOLMapCamera::ApplyOrbitDelta(MapCamMakeState(focus, 3.0, 0.0, 1.0e9), 0.3, 0.0, params);
    TestTrue(TEXT("3.0 + 0.3 wraps to 3.3 - 2PI"), FMath::Abs(overPi.YawRad - (3.3 - UE_DOUBLE_TWO_PI)) <= 1.0e-12);
    TestTrue(TEXT("Wrapped yaw is <= PI"), overPi.YawRad <= UE_DOUBLE_PI);

    const FSOLOrbitCameraState underPi =
        SOLMapCamera::ApplyOrbitDelta(MapCamMakeState(focus, -3.0, 0.0, 1.0e9), -0.3, 0.0, params);
    TestTrue(TEXT("-3.0 - 0.3 wraps to 2PI - 3.3"), FMath::Abs(underPi.YawRad - (UE_DOUBLE_TWO_PI - 3.3)) <= 1.0e-12);
    TestTrue(TEXT("Wrapped yaw is > -PI"), underPi.YawRad > -UE_DOUBLE_PI);

    const FSOLOrbitCameraState toPi =
        SOLMapCamera::ApplyOrbitDelta(MapCamMakeState(focus, 0.0, 0.0, 1.0e9), UE_DOUBLE_PI, 0.0, params);
    TestTrue(TEXT("0 + PI stays PI (upper bound inclusive)"), FMath::Abs(toPi.YawRad - UE_DOUBLE_PI) <= 1.0e-12);

    const FSOLOrbitCameraState toMinusPi =
        SOLMapCamera::ApplyOrbitDelta(MapCamMakeState(focus, 0.0, 0.0, 1.0e9), -UE_DOUBLE_PI, 0.0, params);
    TestTrue(TEXT("0 - PI maps to +PI (lower bound exclusive)"),
        FMath::Abs(toMinusPi.YawRad - UE_DOUBLE_PI) <= 1.0e-12);
    TestTrue(TEXT("0 - PI result is > -PI"), toMinusPi.YawRad > -UE_DOUBLE_PI);

    const FSOLOrbitCameraState multi = SOLMapCamera::ApplyOrbitDelta(
        MapCamMakeState(focus, 0.25, 0.0, 1.0e9), 20.0 * UE_DOUBLE_TWO_PI, 0.0, params);
    TestTrue(TEXT("20 full turns reduce back to 0.25"), FMath::Abs(multi.YawRad - 0.25) <= 1.0e-9);

    const FSOLOrbitCameraState multiNeg = SOLMapCamera::ApplyOrbitDelta(
        MapCamMakeState(focus, 0.25, 0.0, 1.0e9), -7.0 * UE_DOUBLE_TWO_PI - 0.5, 0.0, params);
    TestTrue(TEXT("-7 turns - 0.5 reduces to -0.25"), FMath::Abs(multiNeg.YawRad + 0.25) <= 1.0e-9);
    TestTrue(TEXT("Focus unchanged by wrapping"), multiNeg.FocusPositionM == focus);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraOrbitPitchTest, "SOLTest.MapCamera.OrbitPitchAddsAndClamps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Pitch adds inside the range and saturates exactly at MinPitchRad/MaxPitchRad for huge deltas (no overshoot, no wrap)
bool FSOLMapCameraOrbitPitchTest::RunTest(const FString& /*parameters*/)
{
    const FSOLOrbitCameraParams params;
    const FSOLOrbitCameraState start = MapCamMakeState(FVector3d(4.0e10, 0.0, -1.0e9), 1.0, -0.5, 2.0e10);

    const FSOLOrbitCameraState inside = SOLMapCamera::ApplyOrbitDelta(start, 0.0, 0.8, params);
    TestTrue(TEXT("-0.5 + 0.8 = 0.3"), FMath::Abs(inside.PitchRad - 0.3) <= MAP_CAM_REL_TOL);
    TestEqual(TEXT("Yaw unchanged for zero yaw delta"), inside.YawRad, start.YawRad);

    const FSOLOrbitCameraState up = SOLMapCamera::ApplyOrbitDelta(start, 0.0, 100.0, params);
    TestEqual(TEXT("Huge +delta saturates at MaxPitchRad"), up.PitchRad, params.MaxPitchRad);
    const FSOLOrbitCameraState down = SOLMapCamera::ApplyOrbitDelta(start, 0.0, -100.0, params);
    TestEqual(TEXT("Huge -delta saturates at MinPitchRad"), down.PitchRad, params.MinPitchRad);

    // A delta of a full turn must clamp, not wrap back to the start
    const FSOLOrbitCameraState turn = SOLMapCamera::ApplyOrbitDelta(start, 0.0, UE_DOUBLE_TWO_PI, params);
    TestEqual(TEXT("+2PI pitch delta clamps at MaxPitchRad"), turn.PitchRad, params.MaxPitchRad);

    // Custom limits are honored
    FSOLOrbitCameraParams custom;
    custom.MinPitchRad = -1.0;
    custom.MaxPitchRad = 0.8;
    TestEqual(TEXT("Custom max"), SOLMapCamera::ApplyOrbitDelta(start, 0.0, 5.0, custom).PitchRad, 0.8);
    TestEqual(TEXT("Custom min"), SOLMapCamera::ApplyOrbitDelta(start, 0.0, -5.0, custom).PitchRad, -1.0);

    // Yaw and pitch deltas together; focus and distance untouched
    const FSOLOrbitCameraState both = SOLMapCamera::ApplyOrbitDelta(start, -0.2, 0.1, params);
    TestTrue(TEXT("Combined: yaw 0.8"), FMath::Abs(both.YawRad - 0.8) <= MAP_CAM_REL_TOL);
    TestTrue(TEXT("Combined: pitch -0.4"), FMath::Abs(both.PitchRad + 0.4) <= MAP_CAM_REL_TOL);
    TestTrue(TEXT("Combined: focus unchanged"), both.FocusPositionM == start.FocusPositionM);
    TestEqual(TEXT("Combined: distance unchanged"), both.DistanceM, start.DistanceM);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraOrbitZeroTest, "SOLTest.MapCamera.OrbitZeroDeltaNoOp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Zero deltas leave an in-range state unchanged, including yaw exactly at +PI and pitch exactly at a bound
bool FSOLMapCameraOrbitZeroTest::RunTest(const FString& /*parameters*/)
{
    const FSOLOrbitCameraParams params;
    const FSOLOrbitCameraState states[] = {
        MapCamMakeState(FVector3d(1.5e11, 2.0e10, -4.0e9), 0.3, -0.5, 1.0e10),
        MapCamMakeState(FVector3d::ZeroVector, UE_DOUBLE_PI, params.MaxPitchRad, params.MaxDistanceM),
        MapCamMakeState(FVector3d(-1.0, 0.0, 0.0), -3.0, params.MinPitchRad, params.MinDistanceM),
    };
    for (const FSOLOrbitCameraState& state : states)
    {
        const FSOLOrbitCameraState result = SOLMapCamera::ApplyOrbitDelta(state, 0.0, 0.0, params);
        TestTrue(TEXT("Yaw equivalent"),
            FMath::Abs(SOLTestHelpers::AngleDiffRad(result.YawRad - state.YawRad)) <= 1.0e-12);
        TestTrue(TEXT("Yaw in (-PI, PI]"), result.YawRad > -UE_DOUBLE_PI && result.YawRad <= UE_DOUBLE_PI);
        TestEqual(TEXT("Pitch unchanged"), result.PitchRad, state.PitchRad);
        TestTrue(TEXT("Focus unchanged"), result.FocusPositionM == state.FocusPositionM);
        TestEqual(TEXT("Distance unchanged"), result.DistanceM, state.DistanceM);
    }
    return true;
}

// --- ApplyPan (Appendix E Amendment 1: both pan axes are Yaw-only and lie in the ecliptic plane) ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraPanYawZeroTest, "SOLTest.MapCamera.PanAtYawZeroAxes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At Yaw=0 the camera looks along +X: pan right is ecliptic -Y, pan "up" slides forward along +X; diagonals add
bool FSOLMapCameraPanYawZeroTest::RunTest(const FString& /*parameters*/)
{
    const FSOLOrbitCameraState state = MapCamMakeState(FVector3d(5.0, 6.0, 7.0), 0.0, 0.0, 1.0e10);
    TestTrue(TEXT("Right 10 m moves focus by (0, -10, 0)"),
        MapCamPanDisplacement(state, 10.0, 0.0).Equals(FVector3d(0.0, -10.0, 0.0), 1.0e-9));
    TestTrue(TEXT("Up 4 m moves focus forward by (4, 0, 0)"),
        MapCamPanDisplacement(state, 0.0, 4.0).Equals(FVector3d(4.0, 0.0, 0.0), 1.0e-9));
    TestTrue(TEXT("Down (negative up) 2.5 m moves focus back by (-2.5, 0, 0)"),
        MapCamPanDisplacement(state, 0.0, -2.5).Equals(FVector3d(-2.5, 0.0, 0.0), 1.0e-9));
    TestTrue(TEXT("Left (negative right) 3 m moves focus by (0, 3, 0)"),
        MapCamPanDisplacement(state, -3.0, 0.0).Equals(FVector3d(0.0, 3.0, 0.0), 1.0e-9));
    TestTrue(TEXT("Diagonal is the vector sum"),
        MapCamPanDisplacement(state, 10.0, 4.0).Equals(FVector3d(4.0, -10.0, 0.0), 1.0e-9));
    TestTrue(TEXT("Up pan never changes the focus height"), MapCamPanDisplacement(state, 0.0, 4.0).Z == 0.0);

    // The axes agree with the geometric look direction the position contract defines
    TestTrue(TEXT("Pan right agrees with look x Up"),
        MapCamPanDisplacement(state, 1.0, 0.0).Equals(MapCamExpectedRight(state), 1.0e-9));
    TestTrue(TEXT("Pan up agrees with the horizontal look direction"),
        MapCamPanDisplacement(state, 0.0, 1.0).Equals(MapCamExpectedForwardH(state), 1.0e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraPanRightQuarterTurnTest, "SOLTest.MapCamera.PanRightAtYawQuarterTurn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Pins the pan-right sign: at Yaw=+PI/2 (camera on -Y looking toward +Y) pan right D moves the focus by (+D,0,0)
bool FSOLMapCameraPanRightQuarterTurnTest::RunTest(const FString& /*parameters*/)
{
    const double panM = 250.0;
    const FSOLOrbitCameraState state = MapCamMakeState(FVector3d(1.0e3, -2.0e3, 3.0e2), UE_DOUBLE_HALF_PI, 0.4, 1.0e10);
    const FVector3d right = MapCamPanDisplacement(state, panM, 0.0);
    TestTrue(TEXT("Yaw +PI/2: pan right D moves focus by (+D, 0, 0)"),
        right.Equals(FVector3d(panM, 0.0, 0.0), 1.0e-9));
    TestTrue(TEXT("Yaw +PI/2: pan right X is positive (sign pinned)"), right.X > 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraPanYawRotatesTest, "SOLTest.MapCamera.PanAxesRotateWithYaw",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Both pan axes rotate together with Yaw: unit, horizontal, mutually perpendicular, matching the closed-form rotation
bool FSOLMapCameraPanYawRotatesTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d focus(2.0e9, -1.0e9, 5.0e8);
    const FVector3d rightAt0 = MapCamPanDisplacement(MapCamMakeState(focus, 0.0, 0.0, 1.0e10), 1.0, 0.0);
    const FVector3d rightAt90 =
        MapCamPanDisplacement(MapCamMakeState(focus, UE_DOUBLE_HALF_PI, 0.0, 1.0e10), 1.0, 0.0);
    TestTrue(TEXT("Yaw 0 and yaw 90 right are perpendicular"),
        FMath::Abs(FVector3d::DotProduct(rightAt0, rightAt90)) <= 1.0e-6);

    // At Yaw=+PI/2 the forward-horizontal pan is (0,1,0) (camera on -Y looking toward +Y)
    TestTrue(TEXT("Yaw +PI/2: pan up moves focus by (0, +1, 0)"),
        MapCamPanDisplacement(MapCamMakeState(FVector3d::ZeroVector, UE_DOUBLE_HALF_PI, 0.0, 1000.0), 0.0, 1.0)
            .Equals(FVector3d(0.0, 1.0, 0.0), 1.0e-9));

    const double yaws[] = { -2.9, -UE_DOUBLE_HALF_PI, -0.4, 0.0, 0.9, UE_DOUBLE_HALF_PI, 2.6, UE_DOUBLE_PI };
    for (const double yawRad : yaws)
    {
        const FSOLOrbitCameraState state = MapCamMakeState(FVector3d::ZeroVector, yawRad, 0.0, 1000.0);
        const FString label = FString::Printf(TEXT("yaw=%g"), yawRad);
        const FVector3d right = MapCamPanDisplacement(state, 2.0, 0.0) / 2.0;
        const FVector3d forwardH = MapCamPanDisplacement(state, 0.0, 2.0) / 2.0;

        TestTrue(label + TEXT(": pan right == (sin yaw, -cos yaw, 0)"),
            right.Equals(MapCamRightFromYaw(yawRad), 1.0e-9));
        TestTrue(label + TEXT(": pan up == (cos yaw, sin yaw, 0)"),
            forwardH.Equals(MapCamForwardHFromYaw(yawRad), 1.0e-9));
        TestTrue(label + TEXT(": pan right == look x Up"), right.Equals(MapCamExpectedRight(state), 1.0e-9));
        TestTrue(label + TEXT(": pan up == horizontal look direction"),
            forwardH.Equals(MapCamExpectedForwardH(state), 1.0e-9));
        TestTrue(label + TEXT(": pan axes are perpendicular"),
            FMath::Abs(FVector3d::DotProduct(right, forwardH)) <= 1.0e-12);
        TestTrue(label + TEXT(": pan axes are unit length"),
            FMath::Abs(right.Size() - 1.0) <= 1.0e-12 && FMath::Abs(forwardH.Size() - 1.0) <= 1.0e-12);
        TestTrue(label + TEXT(": pan right has exactly zero Z"), right.Z == 0.0);
        TestTrue(label + TEXT(": pan up has exactly zero Z"), forwardH.Z == 0.0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraPanPitchIgnoredTest, "SOLTest.MapCamera.PanIgnoresPitch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Pitch has zero effect on panning: at any pitch the move is exactly the Yaw-only horizontal one and never changes Z
bool FSOLMapCameraPanPitchIgnoredTest::RunTest(const FString& /*parameters*/)
{
    const FSOLOrbitCameraParams params;
    const FVector3d focus(3.0e4, -8.0e3, 1.25e3);
    const double rightM = 7.0;
    const double upM = -3.0;
    const double nearPole = UE_DOUBLE_HALF_PI - 1.0e-3;
    const double yaws[] = { -2.2, 0.0, 0.6, UE_DOUBLE_HALF_PI, UE_DOUBLE_PI };
    const double pitches[] = { params.MinPitchRad, -nearPole, -UE_DOUBLE_HALF_PI, -0.5, 0.0, 0.8, params.MaxPitchRad,
        nearPole, UE_DOUBLE_HALF_PI };
    for (const double yawRad : yaws)
    {
        const FVector3d expected = MapCamRightFromYaw(yawRad) * rightM + MapCamForwardHFromYaw(yawRad) * upM;
        const FVector3d flat = MapCamPanDisplacement(MapCamMakeState(focus, yawRad, 0.0, 1.0e10), rightM, upM);
        for (const double pitchRad : pitches)
        {
            const FSOLOrbitCameraState state = MapCamMakeState(focus, yawRad, pitchRad, 1.0e10);
            const FSOLOrbitCameraState moved = SOLMapCamera::ApplyPan(state, rightM, upM);
            const FVector3d displacement = moved.FocusPositionM - state.FocusPositionM;
            const FString label = FString::Printf(TEXT("yaw=%g pitch=%g"), yawRad, pitchRad);
            TestTrue(label + TEXT(": focus Z is exactly unchanged"), moved.FocusPositionM.Z == focus.Z);
            TestTrue(label + TEXT(": displacement Z is exactly 0"), displacement.Z == 0.0);
            TestTrue(label + TEXT(": XY matches the Yaw-only formula"),
                FMath::Abs(displacement.X - expected.X) <= 1.0e-9
                    && FMath::Abs(displacement.Y - expected.Y) <= 1.0e-9);
            TestTrue(label + TEXT(": equals the pitch-0 pan"), displacement.Equals(flat, 1.0e-9));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraPanPreservesTest, "SOLTest.MapCamera.PanPreservesAnglesAndDistance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Pan leaves Yaw/Pitch/DistanceM untouched and is precision-safe at a ~1e11 m focus; zero pan is a no-op
bool FSOLMapCameraPanPreservesTest::RunTest(const FString& /*parameters*/)
{
    const FSOLOrbitCameraState state = MapCamMakeState(FVector3d(1.5e11, -7.3e10, 2.1e9), 0.0, -0.5, 3.0e9);
    const FSOLOrbitCameraState moved = SOLMapCamera::ApplyPan(state, 1.0, 0.5);
    TestEqual(TEXT("Yaw unchanged"), moved.YawRad, state.YawRad);
    TestEqual(TEXT("Pitch unchanged"), moved.PitchRad, state.PitchRad);
    TestEqual(TEXT("Distance unchanged"), moved.DistanceM, state.DistanceM);
    TestTrue(TEXT("1 m right, 0.5 m forward at a large focus"),
        (moved.FocusPositionM - state.FocusPositionM).Equals(FVector3d(0.5, -1.0, 0.0), MAP_CAM_LARGE_ABS_TOL_M));

    TestTrue(TEXT("Zero pan is a no-op"), MapCamStatesIdentical(SOLMapCamera::ApplyPan(state, 0.0, 0.0), state));
    return true;
}

// --- ApplyZoom ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraZoomStepTest, "SOLTest.MapCamera.ZoomSingleSteps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// One wheel step out multiplies DistanceM by ZoomStepFactor, one step in divides by it; other fields untouched
bool FSOLMapCameraZoomStepTest::RunTest(const FString& /*parameters*/)
{
    const FSOLOrbitCameraParams params;
    const FSOLOrbitCameraState state = MapCamMakeState(FVector3d(1.5e11, 3.0e9, -2.0e8), 0.9, -0.3, 1.0e10);

    const FSOLOrbitCameraState out = SOLMapCamera::ApplyZoom(state, 1, params);
    TestTrue(TEXT("+1 step multiplies by 1.2"),
        SOLTestHelpers::RelativeError(out.DistanceM, 1.2e10) <= MAP_CAM_REL_TOL);
    TestEqual(TEXT("Yaw unchanged"), out.YawRad, state.YawRad);
    TestEqual(TEXT("Pitch unchanged"), out.PitchRad, state.PitchRad);
    TestTrue(TEXT("Focus unchanged"), out.FocusPositionM == state.FocusPositionM);

    const FSOLOrbitCameraState in = SOLMapCamera::ApplyZoom(state, -1, params);
    TestTrue(TEXT("-1 step divides by 1.2"),
        SOLTestHelpers::RelativeError(in.DistanceM, 1.0e10 / 1.2) <= MAP_CAM_REL_TOL);
    TestTrue(TEXT("-1 step: focus unchanged"), in.FocusPositionM == state.FocusPositionM);

    // A custom step factor is honored: 3 steps of 2x = 8x
    FSOLOrbitCameraParams custom;
    custom.ZoomStepFactor = 2.0;
    TestTrue(TEXT("Factor 2, +3 steps = 8x"),
        SOLTestHelpers::RelativeError(SOLMapCamera::ApplyZoom(state, 3, custom).DistanceM, 8.0e10) <= MAP_CAM_REL_TOL);
    TestTrue(TEXT("Factor 2, -2 steps = 1/4"),
        SOLTestHelpers::RelativeError(SOLMapCamera::ApplyZoom(state, -2, custom).DistanceM, 2.5e9) <= MAP_CAM_REL_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraZoomZeroTest, "SOLTest.MapCamera.ZoomZeroStepsNoOp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Zero wheel steps leave an in-range state unchanged (including distance exactly at a bound)
bool FSOLMapCameraZoomZeroTest::RunTest(const FString& /*parameters*/)
{
    const FSOLOrbitCameraParams params;
    const double distances[] = { params.MinDistanceM, 4.2e9, params.MaxDistanceM };
    for (const double distanceM : distances)
    {
        const FSOLOrbitCameraState state = MapCamMakeState(FVector3d(1.0e11, 0.0, 1.0), -1.0, 0.2, distanceM);
        TestTrue(FString::Printf(TEXT("D=%g unchanged"), distanceM),
            MapCamStatesIdentical(SOLMapCamera::ApplyZoom(state, 0, params), state));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraZoomClampTest, "SOLTest.MapCamera.ZoomClampsAtLimits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Large wheel counts, and steps that just cross a limit, clamp exactly at MinDistanceM/MaxDistanceM
bool FSOLMapCameraZoomClampTest::RunTest(const FString& /*parameters*/)
{
    const FSOLOrbitCameraParams params;
    const FSOLOrbitCameraState state = MapCamMakeState(FVector3d(-3.0e10, 1.0e10, 0.0), 0.0, -0.5, 1.0e10);
    TestEqual(TEXT("+500 steps clamps at MaxDistanceM"),
        SOLMapCamera::ApplyZoom(state, 500, params).DistanceM, params.MaxDistanceM);
    TestEqual(TEXT("-500 steps clamps at MinDistanceM"),
        SOLMapCamera::ApplyZoom(state, -500, params).DistanceM, params.MinDistanceM);

    const FSOLOrbitCameraState nearMax = MapCamMakeState(FVector3d::ZeroVector, 0.0, -0.5, 5.5e13);
    TestEqual(TEXT("One step past max clamps exactly"),
        SOLMapCamera::ApplyZoom(nearMax, 1, params).DistanceM, params.MaxDistanceM);
    const FSOLOrbitCameraState nearMin = MapCamMakeState(FVector3d::ZeroVector, 0.0, -0.5, 1.1e6);
    TestEqual(TEXT("One step past min clamps exactly"),
        SOLMapCamera::ApplyZoom(nearMin, -1, params).DistanceM, params.MinDistanceM);

    // Custom limits are honored
    FSOLOrbitCameraParams custom;
    custom.MinDistanceM = 5.0e9;
    custom.MaxDistanceM = 2.0e10;
    TestEqual(TEXT("Custom max"), SOLMapCamera::ApplyZoom(state, 10, custom).DistanceM, 2.0e10);
    TestEqual(TEXT("Custom min"), SOLMapCamera::ApplyZoom(state, -10, custom).DistanceM, 5.0e9);

    const FSOLOrbitCameraState clamped = SOLMapCamera::ApplyZoom(state, 500, params);
    TestTrue(TEXT("Clamped zoom leaves focus unchanged"), clamped.FocusPositionM == state.FocusPositionM);
    TestEqual(TEXT("Clamped zoom leaves yaw unchanged"), clamped.YawRad, state.YawRad);
    TestEqual(TEXT("Clamped zoom leaves pitch unchanged"), clamped.PitchRad, state.PitchRad);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapCameraZoomChainTest, "SOLTest.MapCamera.ZoomChainsMultiplicatively",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Chained zooms compound: two steps in = D / 1.44, three single steps out = one triple step, in-then-out restores D
bool FSOLMapCameraZoomChainTest::RunTest(const FString& /*parameters*/)
{
    const FSOLOrbitCameraParams params;
    const FSOLOrbitCameraState state = MapCamMakeState(FVector3d(7.0e10, 0.0, 0.0), 0.2, 0.1, 1.0e10);

    const FSOLOrbitCameraState twiceIn =
        SOLMapCamera::ApplyZoom(SOLMapCamera::ApplyZoom(state, -1, params), -1, params);
    TestTrue(TEXT("Two steps in = D / 1.44"),
        SOLTestHelpers::RelativeError(twiceIn.DistanceM, 1.0e10 / 1.44) <= 1.0e-12);

    FSOLOrbitCameraState chained = state;
    for (int32 i = 0; i < 3; ++i)
    {
        chained = SOLMapCamera::ApplyZoom(chained, 1, params);
    }
    const FSOLOrbitCameraState triple = SOLMapCamera::ApplyZoom(state, 3, params);
    TestTrue(TEXT("Three single steps = 1.728 D"),
        SOLTestHelpers::RelativeError(chained.DistanceM, 1.728e10) <= 1.0e-12);
    TestTrue(TEXT("One triple step = 1.728 D"),
        SOLTestHelpers::RelativeError(triple.DistanceM, 1.728e10) <= 1.0e-12);

    const FSOLOrbitCameraState roundTrip =
        SOLMapCamera::ApplyZoom(SOLMapCamera::ApplyZoom(state, 4, params), -4, params);
    TestTrue(TEXT("Out 4 then in 4 restores D"),
        SOLTestHelpers::RelativeError(roundTrip.DistanceM, 1.0e10) <= 1.0e-12);

    // Chaining into the clamp: 60 steps out from 1e10 would exceed 6e13, so it stops exactly at max, and zooming
    // back in then starts from max (clamping is not "remembered")
    FSOLOrbitCameraState longChain = state;
    for (int32 i = 0; i < 60; ++i)
    {
        longChain = SOLMapCamera::ApplyZoom(longChain, 1, params);
    }
    TestEqual(TEXT("Long chain stops at max"), longChain.DistanceM, params.MaxDistanceM);
    TestTrue(TEXT("One step in from max = max / 1.2"),
        SOLTestHelpers::RelativeError(SOLMapCamera::ApplyZoom(longChain, -1, params).DistanceM,
            params.MaxDistanceM / 1.2) <= 1.0e-12);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
