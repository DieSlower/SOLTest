/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Ship/SOLShipFragments.h"
#include "Ship/SOLShipPawn.h"
#include "SOLConstants.h"
#include "Visuals/SOLBodyVisuals.h"
#include "Visuals/SOLThrusterMath.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #51 part 51d player plume glue (SDD 51 sections 4.2, 4.3, 4.7 and 4.10), written as contracts
// before the production code: the flight step's world thrust in the ship frame (SOLThruster::ShipThrustLocalMps2, X
// nose, Y right, Z top as the pawn draws the ship), the transient FSOLShipStateFragment::ThrustLocalG, the player's
// plume floats and hull light (SOLThruster::PlayerPlume: the 20 m hull, War power colour, the clamp above G_REF, the
// light from b and c, gated by the caller's on / off), and the hull light component on lighting channel 2, which only
// the player's hull parts share (SOLShipSunlight::AddToPlayerHullLightChannel)

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double PLAYERTHR_TOL = 1.0e-4;
    constexpr double PLAYERTHR_LEN_TOL = 2.0e-3;
    constexpr double PLAYERTHR_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double PLAYERTHR_VIEW_M = 60.0;           // About the chase camera's distance
    constexpr double PLAYERTHR_FOCAL_PX = 1000.0;       // A plausible 1080p-reference focal length

    //////////////////////////////////////////////////////////////////////////
    // Returns true when a double vector agrees with (x, y, z) component-wise within a tolerance
    bool PlayerThrVecNear(const FVector3d& value, const double x, const double y, const double z,
        const double tolerance = 1.0e-6)
    {
        return FMath::Abs(value.X - x) <= tolerance && FMath::Abs(value.Y - y) <= tolerance
            && FMath::Abs(value.Z - z) <= tolerance;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two linear colours agree in r, g and b within a tolerance
    bool PlayerThrColourNear(const FLinearColor& a, const FLinearColor& b)
    {
        return FMath::Abs(a.R - b.R) <= PLAYERTHR_TOL && FMath::Abs(a.G - b.G) <= PLAYERTHR_TOL
            && FMath::Abs(a.B - b.B) <= PLAYERTHR_TOL;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the player plume at a hull-frame thrust (G) with the test view, the light enabled
    FSOLPlayerPlume PlayerThrPlume(const FVector3f& thrustG, const bool bWarPower = false,
        const bool bLightEnabled = true)
    {
        return SOLThruster::PlayerPlume(thrustG, bWarPower, PLAYERTHR_VIEW_M, PLAYERTHR_FOCAL_PX, bLightEnabled);
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerThrusterShipFrameTest, "SOLTest.PlayerThruster.ShipFrame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// ShipThrustLocalMps2(orientation, thrustWorldMps2): the world thrust in the ship frame (orientation is ship-local ->
// universe, Unreal-handed: X nose, Y right, Z top); zero for a non-finite input. Over a step's substeps FSOLThrustMean
// of these gives the player's ThrustLocalG
bool FSOLPlayerThrusterShipFrameTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("identity attitude keeps the vector"), PlayerThrVecNear(SOLThruster::ShipThrustLocalMps2(
        FQuat4d::Identity, FVector3d(1.0, 2.0, 3.0)), 1.0, 2.0, 3.0));

    // A ship yawed 90 degrees and pitched 30: the world thrust along its own axes reads as the local axes
    const FQuat4d attitude = FQuat4d(FVector3d::UpVector, UE_DOUBLE_HALF_PI)
        * FQuat4d(FVector3d::RightVector, -UE_DOUBLE_PI / 6.0);
    TestTrue(TEXT("thrust along the nose is +X"), PlayerThrVecNear(SOLThruster::ShipThrustLocalMps2(attitude,
        attitude.GetForwardVector() * 29.42), 29.42, 0.0, 0.0));
    TestTrue(TEXT("thrust along the ship's right is +Y"), PlayerThrVecNear(SOLThruster::ShipThrustLocalMps2(attitude,
        attitude.GetRightVector() * 19.6), 0.0, 19.6, 0.0));
    TestTrue(TEXT("thrust along the ship's top is +Z"), PlayerThrVecNear(SOLThruster::ShipThrustLocalMps2(attitude,
        attitude.GetUpVector() * 19.6), 0.0, 0.0, 19.6));
    TestTrue(TEXT("a mixed local vector round-trips"), PlayerThrVecNear(SOLThruster::ShipThrustLocalMps2(attitude,
        attitude.RotateVector(FVector3d(-14.7, 3.0, -2.0))), -14.7, 3.0, -2.0));
    TestTrue(TEXT("a NaN thrust gives zero"), PlayerThrVecNear(SOLThruster::ShipThrustLocalMps2(attitude,
        FVector3d(PLAYERTHR_NAN, 0.0, 0.0)), 0.0, 0.0, 0.0));
    TestTrue(TEXT("a NaN attitude gives zero"), PlayerThrVecNear(SOLThruster::ShipThrustLocalMps2(
        FQuat4d(PLAYERTHR_NAN, 0.0, 0.0, 1.0), FVector3d(1.0, 0.0, 0.0)), 0.0, 0.0, 0.0));

    // Two substeps at different attitudes, each burning 3 G along its own nose: the step's mean is 3 G forward
    const double g0 = SOL::STANDARD_GRAVITY_MPS2;
    const FQuat4d turned = FQuat4d(FVector3d::UpVector, 0.4) * attitude;
    FSOLThrustMean mean;
    mean.Add(SOLThruster::ShipThrustLocalMps2(attitude, attitude.GetForwardVector() * 3.0 * g0), 0.01);
    mean.Add(SOLThruster::ShipThrustLocalMps2(turned, turned.GetForwardVector() * 3.0 * g0), 0.01);
    const FVector3f meanG = mean.MeanG();
    TestTrue(TEXT("the substep mean of nose burns is 3 G forward"), PlayerThrVecNear(FVector3d(meanG), 3.0, 0.0, 0.0,
        PLAYERTHR_TOL));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerThrusterPlumeTest, "SOLTest.PlayerThruster.Plume",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// PlayerPlume(...).Plume equals PlumeInstance(thrust.X, bWarPower, PLAYER_HULL_LENGTH_M, PLAYER_NOZZLE_RADIUS_M,
// distance, focal): the SDD 51 section 4.3 player values, clamped above G_REF, c = 1 under War power with b and the
// length from the real G; only the forward component lights it
bool FSOLPlayerThrusterPlumeTest::RunTest(const FString& /*parameters*/)
{
    // SDD 4.3 table, player 20 m hull: 1 G and 3 G
    const FSOLPlumeInstance oneG = PlayerThrPlume(FVector3f(1.0f, 0.0f, 0.0f)).Plume;
    TestNearlyEqual(TEXT("1 G: b"), static_cast<double>(oneG.Brightness), 0.3834, PLAYERTHR_TOL);
    TestNearlyEqual(TEXT("1 G: c (blue-white)"), static_cast<double>(oneG.Colour), 0.0, PLAYERTHR_TOL);
    TestNearlyEqual(TEXT("1 G: length"), static_cast<double>(oneG.LengthM), 4.314, PLAYERTHR_LEN_TOL);
    const FSOLPlumeInstance threeG = PlayerThrPlume(FVector3f(3.0f, 0.0f, 0.0f)).Plume;
    TestNearlyEqual(TEXT("3 G: b"), static_cast<double>(threeG.Brightness), 0.6641, PLAYERTHR_TOL);
    TestNearlyEqual(TEXT("3 G: c (yellow-white)"), static_cast<double>(threeG.Colour), 0.35, PLAYERTHR_TOL);
    TestNearlyEqual(TEXT("3 G: length"), static_cast<double>(threeG.LengthM), 10.388, PLAYERTHR_LEN_TOL);

    // Boost (9 G) clamps at G_REF: the hull length, b 1, c 1 (red-orange)
    const FSOLPlumeInstance boost = PlayerThrPlume(FVector3f(9.0f, 0.0f, 0.0f)).Plume;
    TestNearlyEqual(TEXT("9 G: length clamped to the hull"), static_cast<double>(boost.LengthM),
        SOL::Thruster::PLAYER_HULL_LENGTH_M, PLAYERTHR_LEN_TOL);
    TestNearlyEqual(TEXT("9 G: b clamped"), static_cast<double>(boost.Brightness), 1.0, PLAYERTHR_TOL);
    TestNearlyEqual(TEXT("9 G: c clamped"), static_cast<double>(boost.Colour), 1.0, PLAYERTHR_TOL);

    // War power: red-orange at the 1.5 G recovery, b and length from the real G
    const FSOLPlumeInstance war = PlayerThrPlume(FVector3f(1.5f, 0.0f, 0.0f), true).Plume;
    TestNearlyEqual(TEXT("War power: c forced to 1"), static_cast<double>(war.Colour), 1.0, PLAYERTHR_TOL);
    TestNearlyEqual(TEXT("War power: b follows the real G"), static_cast<double>(war.Brightness),
        SOLThruster::PlumeBrightness(1.5), PLAYERTHR_TOL);
    TestNearlyEqual(TEXT("War power: length follows the real G"), static_cast<double>(war.LengthM),
        SOLThruster::PlumeLengthM(SOL::Thruster::PLAYER_HULL_LENGTH_M, 1.5), PLAYERTHR_LEN_TOL);

    // The floats are PlumeInstance's of the player hull, the side components ignored
    const FSOLPlumeInstance expected = SOLThruster::PlumeInstance(2.2, false, SOL::Thruster::PLAYER_HULL_LENGTH_M,
        SOL::Thruster::PLAYER_NOZZLE_RADIUS_M, PLAYERTHR_VIEW_M, PLAYERTHR_FOCAL_PX);
    const FSOLPlumeInstance mixed = PlayerThrPlume(FVector3f(2.2f, 1.3f, -0.7f)).Plume;
    TestTrue(TEXT("floats equal PlumeInstance of the forward G"), FMath::IsNearlyEqual(mixed.Colour, expected.Colour,
        1.0e-6f) && FMath::IsNearlyEqual(mixed.Brightness, expected.Brightness, 1.0e-6f)
        && FMath::IsNearlyEqual(mixed.LengthM, expected.LengthM, 1.0e-5f));

    // No main plume for reverse, lateral or vertical thrust alone, nor for a NaN, nor while idle
    for (const FVector3f& thrust : { FVector3f(-1.5f, 0.0f, 0.0f), FVector3f(0.0f, 2.0f, 0.0f),
        FVector3f(0.0f, 0.0f, -2.0f), FVector3f(0.0f, 0.0f, 0.0f),
        FVector3f(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f) })
    {
        const FSOLPlayerPlume off = PlayerThrPlume(thrust);
        TestTrue(*FString::Printf(TEXT("no plume at (%.1f, %.1f, %.1f) G"), thrust.X, thrust.Y, thrust.Z),
            off.Plume.Colour == 0.0f && off.Plume.Brightness == 0.0f && off.Plume.LengthM == 0.0f);
        TestFalse(*FString::Printf(TEXT("no light at (%.1f, %.1f, %.1f) G"), thrust.X, thrust.Y, thrust.Z),
            off.bLightVisible);
        TestEqual(TEXT("...and zero light intensity"), off.LightIntensityCd, 0.0f);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerThrusterLightTest, "SOLTest.PlayerThruster.Light",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// PlayerPlume's hull light (section 4.7): visible while enabled and b > 0, intensity b x PLAYER_LIGHT_MAX_INTENSITY_CD,
// colour the ramp's rim colour at c (red-orange under War power); b is the real G's, not cut by the plume's pixel size
bool FSOLPlayerThrusterLightTest::RunTest(const FString& /*parameters*/)
{
    const double maxCd = SOL::Thruster::PLAYER_LIGHT_MAX_INTENSITY_CD;
    const FSOLPlayerPlume oneG = PlayerThrPlume(FVector3f(1.0f, 0.0f, 0.0f));
    TestTrue(TEXT("1 G: light visible"), oneG.bLightVisible);
    TestNearlyEqual(TEXT("1 G: intensity b x max"), static_cast<double>(oneG.LightIntensityCd), 0.3834 * maxCd,
        PLAYERTHR_TOL * maxCd);
    TestTrue(TEXT("1 G: blue rim colour"), PlayerThrColourNear(oneG.LightColour, SOL::Thruster::RIM_BLUE));

    const FSOLPlayerPlume threeG = PlayerThrPlume(FVector3f(3.0f, 0.0f, 0.0f));
    TestNearlyEqual(TEXT("3 G: intensity b x max"), static_cast<double>(threeG.LightIntensityCd), 0.6641 * maxCd,
        PLAYERTHR_TOL * maxCd);
    TestTrue(TEXT("3 G: yellow rim colour"), PlayerThrColourNear(threeG.LightColour, SOL::Thruster::RIM_YELLOW));

    const FSOLPlayerPlume boost = PlayerThrPlume(FVector3f(9.0f, 0.0f, 0.0f));
    TestNearlyEqual(TEXT("9 G: intensity clamped at the max"), static_cast<double>(boost.LightIntensityCd), maxCd,
        PLAYERTHR_TOL * maxCd);
    TestTrue(TEXT("9 G: red rim colour"), PlayerThrColourNear(boost.LightColour, SOL::Thruster::RIM_RED));

    const FSOLPlayerPlume war = PlayerThrPlume(FVector3f(1.5f, 0.0f, 0.0f), true);
    TestTrue(TEXT("War power: red rim colour at 1.5 G"), PlayerThrColourNear(war.LightColour,
        SOL::Thruster::RIM_RED));
    TestNearlyEqual(TEXT("War power: intensity from the real G"), static_cast<double>(war.LightIntensityCd),
        SOLThruster::PlumeBrightness(1.5) * maxCd, PLAYERTHR_TOL * maxCd);

    // Disabled (Low quality outside VR, decided by the caller with PlayerLightEnabled): hidden, zero, plume unchanged
    const FSOLPlayerPlume disabled = PlayerThrPlume(FVector3f(3.0f, 0.0f, 0.0f), false, false);
    TestFalse(TEXT("disabled: light hidden"), disabled.bLightVisible);
    TestEqual(TEXT("disabled: zero intensity"), disabled.LightIntensityCd, 0.0f);
    TestNearlyEqual(TEXT("disabled: the plume still burns"), static_cast<double>(disabled.Plume.Brightness), 0.6641,
        PLAYERTHR_TOL);

    // Far away (the nozzle glow under its 0.5 px fade): no plume floats, the light still follows b
    const FSOLPlayerPlume far = SOLThruster::PlayerPlume(FVector3f(3.0f, 0.0f, 0.0f), false, 1.0e9, PLAYERTHR_FOCAL_PX,
        true);
    TestEqual(TEXT("far: plume cut"), far.Plume.Brightness, 0.0f);
    TestTrue(TEXT("far: light still on"), far.bLightVisible && far.LightIntensityCd > 0.0f);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerThrusterConstantsTest, "SOLTest.PlayerThruster.Constants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The hull light constants (section 4.7): a positive start intensity, the hull length as its radius, a little aft of
// the nozzles' centroid on the hull's centre line
bool FSOLPlayerThrusterConstantsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Thruster;
    TestTrue(TEXT("max intensity positive"), PLAYER_LIGHT_MAX_INTENSITY_CD > 0.0);
    TestEqual(TEXT("radius is the hull length"), PLAYER_LIGHT_RADIUS_M, PLAYER_HULL_LENGTH_M);
    TestTrue(TEXT("a little aft of the nozzles"), PLAYER_LIGHT_AFT_OF_NOZZLES_M > 0.0
        && PLAYER_LIGHT_AFT_OF_NOZZLES_M < 0.25 * PLAYER_HULL_LENGTH_M);
    const FSOLHullThrusters& player = SOLThruster::HullThrusters(ESOLHullKind::Player);
    TestEqual(TEXT("the player has two nozzles"), player.NozzleCount, 2);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerThrusterStateFieldTest, "SOLTest.PlayerThruster.StateFieldNotSaved",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// FSOLShipStateFragment::ThrustLocalG (section 4.2): zero on a new fragment, a plain field, not a reflected property
bool FSOLPlayerThrusterStateFieldTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("a new fragment has no thrust"), FSOLShipStateFragment().ThrustLocalG.IsZero());
    TestNull(TEXT("not a reflected property"), FindFProperty<FProperty>(FSOLShipStateFragment::StaticStruct(),
        TEXT("ThrustLocalG")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerThrusterHullLightTest, "SOLTest.PlayerThruster.HullLightComponent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The pawn's hull light (Q17, Q28): an unshadowed movable point light in candelas on lighting channel 2 only, the hull
// length as its radius, aft of the nozzles' centroid, hidden until the plume burns; the hull parts join channel 2
// (SOLShipSunlight::AddToPlayerHullLightChannel) and keep channel 1 for the sun and fill lights
bool FSOLPlayerThrusterHullLightTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Thruster;
    UStaticMeshComponent* part = NewObject<UStaticMeshComponent>();
    SOLShipSunlight::PutOnShipLightingChannel(part);
    SOLShipSunlight::AddToPlayerHullLightChannel(part);
    TestTrue(TEXT("hull part on channels 1 and 2"), !part->LightingChannels.bChannel0
        && part->LightingChannels.bChannel1 && part->LightingChannels.bChannel2);
    SOLShipSunlight::AddToPlayerHullLightChannel(nullptr);   // must not crash

    const ASOLShipPawn* pawn = GetDefault<ASOLShipPawn>();
    const UPointLightComponent* light = pawn->FindComponentByClass<UPointLightComponent>();
    if (!TestNotNull(TEXT("the pawn has the hull light"), light))
    {
        return true;
    }
    TestTrue(TEXT("channel 2 only"), !light->LightingChannels.bChannel0 && !light->LightingChannels.bChannel1
        && light->LightingChannels.bChannel2);
    TestFalse(TEXT("no shadows"), light->CastShadows);
    TestTrue(TEXT("movable"), light->Mobility == EComponentMobility::Movable);
    TestTrue(TEXT("candela units"), light->IntensityUnits == ELightUnits::Candelas);
    TestNearlyEqual(TEXT("radius the hull length (cm)"), static_cast<double>(light->AttenuationRadius),
        PLAYER_LIGHT_RADIUS_M * SOL::METERS_TO_CM, 1.0e-3);
    const FVector location = light->GetRelativeLocation();
    TestTrue(TEXT("aft of the nozzles on the centre line"), PlayerThrVecNear(FVector3d(location),
        (PLAYER_NOZZLE_X_M - PLAYER_LIGHT_AFT_OF_NOZZLES_M) * SOL::METERS_TO_CM, 0.0,
        PLAYER_NOZZLE_Z_M * SOL::METERS_TO_CM, 1.0e-3));
    TestFalse(TEXT("hidden until the plume burns"), light->IsVisible());
    return true;
}

#endif
