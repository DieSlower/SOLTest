/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Combat/SOLCombatVisuals.h"
#include "MinorBodies/SOLRingShadowMath.h"
#include "SOLConstants.h"
#include "Ship/SOLShipPawn.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLSimClock.h"
#include "Universe/SOLSimClockSubsystem.h"
#include "Visuals/SOLBodyVisuals.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"

#include <cmath>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// World-level checks of ASOLBodyVisuals' ship sunlight (SDD 16): the planet and ring shadow at the observer dims a
// second, channel-1-only directional light for the ship and dropped targets. A bare game world with the universe
// subsystems and play begun, so the actor runs its real BeginPlay/EndPlay. Written against the contract, not the
// implementation. Helpers here carry names unique across the test files so unity builds do not collide.
namespace
{
    // Tolerances
    constexpr double SHIPSUN_LIT_TOL = 1.0e-6;
    constexpr double SHIPSUN_INTENSITY_REL_TOL = 1.0e-3;
    constexpr double SHIPSUN_ROTATION_TOL_RAD = 1.0e-4;

    // Placement of the ring-shadow case (m): B ring radius and the observer's distance below it
    constexpr double SHIPSUN_B_RING_RADIUS_M = 1.05e8;
    constexpr double SHIPSUN_BELOW_RING_M = 1.0e6;
    constexpr double SHIPSUN_MIN_SUN_ELEVATION_SIN = 0.05;
    constexpr const TCHAR* SHIPSUN_PINNED_DATE = TEXT("2026-10-13T00:00:00Z");

    // A temporary game world with begun play (no game mode, no player), destroyed at scope exit
    struct FSOLShipSunlightTestWorld
    {
        UWorld* World = nullptr;

        //////////////////////////////////////////////////////////////////////////
        // Creates the world and its context and marks play as begun so spawned actors run BeginPlay
        FSOLShipSunlightTestWorld()
        {
            World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SOLShipSunlightTestWorld"));
            FWorldContext& context = GEngine->CreateNewWorldContext(EWorldType::Game);
            context.SetCurrentWorld(World);
            World->InitializeActorsForPlay(FURL());
            World->SetBegunPlay(true);

            // Pin the sim date (the SDD 16 verification date: the Sun ~25 deg below Saturn's ring plane) so the
            // geometry does not depend on the day the tests run
            double julianDate = 0.0;
            USOLSimClockSubsystem* clock = World->GetSubsystem<USOLSimClockSubsystem>();
            USOLBodyRegistrySubsystem* registry = World->GetSubsystem<USOLBodyRegistrySubsystem>();
            if (clock != nullptr && registry != nullptr &&
                FSOLSimClock::TryParseUtcJulianDate(SHIPSUN_PINNED_DATE, julianDate))
            {
                clock->SetJulianDate(julianDate);
                registry->UpdateFromClock();
            }
        }

        //////////////////////////////////////////////////////////////////////////
        // Ends play on every actor, then destroys the world and its context
        ~FSOLShipSunlightTestWorld()
        {
            for (TActorIterator<AActor> it(World); it; ++it)
            {
                it->RouteEndPlay(EEndPlayReason::Quit);
            }
            World->SetBegunPlay(false);
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }

        //////////////////////////////////////////////////////////////////////////
        // Returns the body registry, or nullptr without its subsystem
        const FSOLBodyRegistry* Registry() const
        {
            const USOLBodyRegistrySubsystem* subsystem = World->GetSubsystem<USOLBodyRegistrySubsystem>();
            return subsystem != nullptr ? &subsystem->GetRegistry() : nullptr;
        }

        //////////////////////////////////////////////////////////////////////////
        // Returns a body's registry index, or INDEX_NONE without the registry
        int32 FindBody(const TCHAR* name) const
        {
            const FSOLBodyRegistry* registry = Registry();
            return registry != nullptr ? registry->FindByName(FName(name)) : INDEX_NONE;
        }

        //////////////////////////////////////////////////////////////////////////
        // Moves the observer to an ecliptic position (m) and runs one universe update
        void PlaceObserverAndUpdate(const FVector3d& positionM) const
        {
            USOLAnchorSubsystem* anchor = World->GetSubsystem<USOLAnchorSubsystem>();
            anchor->SetObserverPositionM(positionM);
            anchor->OnUniverseUpdated().Broadcast();
        }

        //////////////////////////////////////////////////////////////////////////
        // Returns the unit vector from a body toward the Sun
        FVector3d TowardSunFrom(const int32 bodyIndex) const
        {
            const FSOLBodyRegistry& registry = *Registry();
            return (registry.GetPositionM(FindBody(SOL::BodyNames::SUN)) - registry.GetPositionM(bodyIndex))
                .GetSafeNormal();
        }

        //////////////////////////////////////////////////////////////////////////
        // Returns the point radii planet radii from a body's centre, toward (+1) or away from (-1) the Sun
        FVector3d AlongSunLine(const int32 bodyIndex, const double radii) const
        {
            const FSOLBodyRegistry& registry = *Registry();
            return registry.GetPositionM(bodyIndex) +
                TowardSunFrom(bodyIndex) * (radii * registry.GetRadiusM(bodyIndex));
        }
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns |a - b| / max(|b|, tiny)
    double ShipSunRelDiff(const double a, const double b)
    {
        return std::fabs(a - b) / FMath::Max(std::fabs(b), 1.0e-300);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the contract's one-body transmittance of Saturn (planet and rings) at an observer
    double ShipSunSaturnOnly(const FSOLBodyRegistry& registry, const int32 saturn, const int32 sun,
        const FVector3d& observerM)
    {
        const FVector3d toSun = registry.GetPositionM(sun) - observerM;
        const FVector3d pole = registry.GetOrientation(saturn).RotateVector(FVector3d(0.0, 0.0, 1.0));
        return SOLRingShadow::BodySunTransmittance(observerM - registry.GetPositionM(saturn), toSun,
            registry.GetRadiusM(saturn), SOLRingShadow::SunAngularRadiusRad(toSun.Length()), pole,
            SOL::RING_SHADOW_LAYER_HALF_THICKNESS_M,
            SOLRingShadow::RealTauProfile(SOLRingShadow::ProfileIndexForPlanet(registry.GetName(saturn))));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the contract's product over every body but the Sun at an observer, built independently of the production
    // loop: the planet term from PlanetShadowTransmittance, and for the four ringed planets (named here, not looked up
    // through ProfileIndexForPlanet) the ring term from RingShadowTransmittance in a ring frame built by Gram-Schmidt
    double ShipSunAllBodies(const FSOLBodyRegistry& registry, const int32 sun, const FVector3d& observerM)
    {
        struct FRinged { const TCHAR* Name; int32 Profile; };
        const FRinged ringed[] = { { SOL::BodyNames::JUPITER, SOL::RING_PROFILE_INDEX_JUPITER },
            { SOL::BodyNames::SATURN, SOL::RING_PROFILE_INDEX_SATURN },
            { SOL::BodyNames::URANUS, SOL::RING_PROFILE_INDEX_URANUS },
            { SOL::BodyNames::NEPTUNE, SOL::RING_PROFILE_INDEX_NEPTUNE } };
        const FVector3d toSun = registry.GetPositionM(sun) - observerM;
        const double alpha = std::asin(FMath::Min(1.0, 6.957e8 / toSun.Length()));   // own Sun radius (IAU 2015)
        double product = 1.0;
        for (int32 i = 0; i < registry.Num(); ++i)
        {
            if (i == sun)
            {
                continue;
            }
            const FVector3d rel = observerM - registry.GetPositionM(i);
            product *= SOLRingShadow::PlanetShadowTransmittance(rel, toSun, registry.GetRadiusM(i), alpha);
            for (const FRinged& entry : ringed)
            {
                if (registry.GetName(i) != FName(entry.Name))
                {
                    continue;
                }
                // Ring frame: z = pole, x = ecliptic X (or Y) with its pole component removed, y = z x x
                const FVector3d z = registry.GetOrientation(i).RotateVector(FVector3d(0.0, 0.0, 1.0)).GetSafeNormal();
                const FVector3d seed = std::fabs(z.X) < 0.9 ? FVector3d(1.0, 0.0, 0.0) : FVector3d(0.0, 1.0, 0.0);
                const FVector3d x = (seed - z * FVector3d::DotProduct(seed, z)).GetSafeNormal();
                const FVector3d y = FVector3d::CrossProduct(z, x);
                const FVector3d receiver(FVector3d::DotProduct(rel, x), FVector3d::DotProduct(rel, y),
                    FVector3d::DotProduct(rel, z));
                const FVector3d sunRing(FVector3d::DotProduct(toSun, x), FVector3d::DotProduct(toSun, y),
                    FVector3d::DotProduct(toSun, z));
                product *= SOLRingShadow::RingShadowTransmittance(receiver, sunRing, alpha,
                    SOL::RING_SHADOW_LAYER_HALF_THICKNESS_M, SOLRingShadow::RealTauProfile(entry.Profile));
            }
        }
        return product;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true if a light lights exactly the given lighting channels
    bool ShipSunHasChannels(const UDirectionalLightComponent* light, const bool bChannel0, const bool bChannel1,
        const bool bChannel2)
    {
        return light != nullptr && light->LightingChannels.bChannel0 == bChannel0 &&
            light->LightingChannels.bChannel1 == bChannel1 && light->LightingChannels.bChannel2 == bChannel2;
    }
}

//////////////////////////////////////////////////////////////////////////
// Checks the fully lit state: ship light hidden, sun light on channels 0 and 1, ship light on channel 1 only
static void ShipSunExpectLit(FAutomationTestBase& test, const ASOLBodyVisuals* visuals, const TCHAR* label)
{
    const UDirectionalLightComponent* sunLight = visuals->GetSunLight();
    const UDirectionalLightComponent* shipLight = visuals->GetShipSunLight();
    if (!test.TestNotNull(*FString::Printf(TEXT("%s: sun light exists"), label), sunLight) ||
        !test.TestNotNull(*FString::Printf(TEXT("%s: ship sun light exists"), label), shipLight))
    {
        return;
    }
    test.TestFalse(*FString::Printf(TEXT("%s: ship sun light hidden"), label), shipLight->IsVisible());
    test.TestTrue(*FString::Printf(TEXT("%s: sun light lights channel 0"), label),
        sunLight->LightingChannels.bChannel0);
    test.TestTrue(*FString::Printf(TEXT("%s: sun light lights channel 1"), label),
        sunLight->LightingChannels.bChannel1);
    test.TestTrue(*FString::Printf(TEXT("%s: ship sun light lights channel 1 only"), label),
        ShipSunHasChannels(shipLight, false, true, false));
}

//////////////////////////////////////////////////////////////////////////
// Checks the shadowed state: ship light visible at sun intensity * T with the sun light's rotation, sun light on
// channel 0 only
static void ShipSunExpectShadowed(FAutomationTestBase& test, const ASOLBodyVisuals* visuals, const double transmittance,
    const TCHAR* label)
{
    const UDirectionalLightComponent* sunLight = visuals->GetSunLight();
    const UDirectionalLightComponent* shipLight = visuals->GetShipSunLight();
    if (!test.TestNotNull(*FString::Printf(TEXT("%s: sun light exists"), label), sunLight) ||
        !test.TestNotNull(*FString::Printf(TEXT("%s: ship sun light exists"), label), shipLight))
    {
        return;
    }
    test.TestTrue(*FString::Printf(TEXT("%s: ship sun light visible"), label), shipLight->IsVisible());
    test.TestTrue(*FString::Printf(TEXT("%s: sun light lights channel 0"), label),
        sunLight->LightingChannels.bChannel0);
    test.TestFalse(*FString::Printf(TEXT("%s: sun light does not light channel 1"), label),
        sunLight->LightingChannels.bChannel1);
    test.TestTrue(*FString::Printf(TEXT("%s: ship sun light lights channel 1 only"), label),
        ShipSunHasChannels(shipLight, false, true, false));

    // Intensity is the sun light's times T (absolute check near 0, relative otherwise)
    const double expected = static_cast<double>(sunLight->Intensity) * transmittance;
    const double actual = shipLight->Intensity;
    const bool bIntensityOk = expected < 1.0e-6 ? std::fabs(actual - expected) < 1.0e-6 * FMath::Max(
        static_cast<double>(sunLight->Intensity), 1.0) : ShipSunRelDiff(actual, expected) < SHIPSUN_INTENSITY_REL_TOL;
    test.TestTrue(*FString::Printf(TEXT("%s: ship sun intensity %.9g ~ sun %.9g * T %.9g"), label, actual,
        static_cast<double>(sunLight->Intensity), transmittance), bIntensityOk);

    const double angle = sunLight->GetComponentQuat().AngularDistance(shipLight->GetComponentQuat());
    test.TestTrue(*FString::Printf(TEXT("%s: ship sun light shares the sun light's rotation (%g rad)"), label, angle),
        angle < SHIPSUN_ROTATION_TOL_RAD);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLShipSunlightLitTest, "SOLTest.ShipSunlight.LitOnSunSide",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// T is 1 before any update; on Saturn's Sun side it stays 1, the ship light is hidden and the sun light lights both
// the world (channel 0) and the ship (channel 1)
bool FSOLShipSunlightLitTest::RunTest(const FString& /*parameters*/)
{
    FSOLShipSunlightTestWorld testWorld;
    const int32 saturn = testWorld.FindBody(SOL::BodyNames::SATURN);
    if (!TestTrue(TEXT("Universe subsystems exist in the test world"), saturn != INDEX_NONE))
    {
        return false;
    }
    ASOLBodyVisuals* visuals = testWorld.World->SpawnActor<ASOLBodyVisuals>();
    if (!TestNotNull(TEXT("Body visuals spawned"), visuals))
    {
        return false;
    }

    TestEqual(TEXT("T is 1 before any update"), visuals->GetShipSunTransmittance(), 1.0);
    TestTrue(TEXT("Ship sun light lights channel 1 only"), ShipSunHasChannels(visuals->GetShipSunLight(), false, true,
        false));
    TestTrue(TEXT("Thresholds: 0.99 < enter < leave < 1"), SOL::SHIP_SUN_SHADOW_ENTER_BELOW > 0.99 &&
        SOL::SHIP_SUN_SHADOW_ENTER_BELOW < SOL::SHIP_SUN_SHADOW_LEAVE_AT && SOL::SHIP_SUN_SHADOW_LEAVE_AT < 1.0);

    testWorld.PlaceObserverAndUpdate(testWorld.AlongSunLine(saturn, 3.0));
    TestTrue(FString::Printf(TEXT("T ~ 1 on the Sun side (%.17g)"), visuals->GetShipSunTransmittance()),
        std::fabs(visuals->GetShipSunTransmittance() - 1.0) < SHIPSUN_LIT_TOL);
    ShipSunExpectLit(*this, visuals, TEXT("Sun side"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLShipSunlightEclipseTest, "SOLTest.ShipSunlight.EclipseBehindPlanet",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Behind Saturn (anti-Sun side, 3 radii) T is ~0: the ship light is on at ~0 intensity and the sun light no longer
// lights channel 1
bool FSOLShipSunlightEclipseTest::RunTest(const FString& /*parameters*/)
{
    FSOLShipSunlightTestWorld testWorld;
    const int32 saturn = testWorld.FindBody(SOL::BodyNames::SATURN);
    if (!TestTrue(TEXT("Universe subsystems exist in the test world"), saturn != INDEX_NONE))
    {
        return false;
    }
    ASOLBodyVisuals* visuals = testWorld.World->SpawnActor<ASOLBodyVisuals>();
    if (!TestNotNull(TEXT("Body visuals spawned"), visuals))
    {
        return false;
    }

    testWorld.PlaceObserverAndUpdate(testWorld.AlongSunLine(saturn, -3.0));
    const double transmittance = visuals->GetShipSunTransmittance();
    TestTrue(FString::Printf(TEXT("T ~ 0 in Saturn's umbra (%.17g)"), transmittance), transmittance >= 0.0 &&
        transmittance < 1.0e-6);
    ShipSunExpectShadowed(*this, visuals, transmittance, TEXT("Eclipse"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLShipSunlightRingShadowTest, "SOLTest.ShipSunlight.RingShadowUnderBRing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// 1000 km under the B ring on its dark side T matches the contract's product (dominated by Saturn) and is < 0.5;
// the ship light carries the dimmed sunlight
bool FSOLShipSunlightRingShadowTest::RunTest(const FString& /*parameters*/)
{
    FSOLShipSunlightTestWorld testWorld;
    const int32 saturn = testWorld.FindBody(SOL::BodyNames::SATURN);
    const int32 sun = testWorld.FindBody(SOL::BodyNames::SUN);
    if (!TestTrue(TEXT("Universe subsystems exist in the test world"), saturn != INDEX_NONE && sun != INDEX_NONE))
    {
        return false;
    }
    const FSOLBodyRegistry& registry = *testWorld.Registry();

    // Sun direction and pole at the current sim date; a Sun near the ring plane casts no usable shadow
    const FVector3d s = testWorld.TowardSunFrom(saturn);
    const FVector3d n = registry.GetOrientation(saturn).RotateVector(FVector3d(0.0, 0.0, 1.0)).GetSafeNormal();
    const double mu = FVector3d::DotProduct(s, n);
    if (std::fabs(mu) < SHIPSUN_MIN_SUN_ELEVATION_SIN)
    {
        AddWarning(FString::Printf(TEXT("Sun is %.4f (sin) from Saturn's ring plane at this sim date; ring shadow case "
            "skipped"), mu));
        return true;
    }

    ASOLBodyVisuals* visuals = testWorld.World->SpawnActor<ASOLBodyVisuals>();
    if (!TestNotNull(TEXT("Body visuals spawned"), visuals))
    {
        return false;
    }

    // The ray to the Sun pierces the plane at 105,000 km on the Sun-ward side; the observer is 1000 km back along it
    const FVector3d inPlane = (s - n * mu).GetSafeNormal();
    const FVector3d hit = registry.GetPositionM(saturn) + inPlane * SHIPSUN_B_RING_RADIUS_M;
    const FVector3d observer = hit - s * SHIPSUN_BELOW_RING_M;
    testWorld.PlaceObserverAndUpdate(observer);

    const double transmittance = visuals->GetShipSunTransmittance();
    const double saturnOnly = ShipSunSaturnOnly(registry, saturn, sun, observer);
    const double allBodies = ShipSunAllBodies(registry, sun, observer);
    TestTrue(FString::Printf(TEXT("T %.17g matches the all-body product %.17g"), transmittance, allBodies),
        ShipSunRelDiff(transmittance, allBodies) < 1.0e-9 || std::fabs(transmittance - allBodies) < 1.0e-15);
    TestTrue(FString::Printf(TEXT("T %.17g <= Saturn alone %.17g"), transmittance, saturnOnly),
        transmittance <= saturnOnly + 1.0e-9);
    TestTrue(FString::Printf(TEXT("T %.17g >= 0.999 Saturn alone %.17g"), transmittance, saturnOnly),
        transmittance >= saturnOnly * 0.999);
    TestTrue(FString::Printf(TEXT("T %.17g < 0.5 under the B ring"), transmittance), transmittance < 0.5);
    ShipSunExpectShadowed(*this, visuals, transmittance, TEXT("Ring shadow"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLShipSunlightReturnTest, "SOLTest.ShipSunlight.ReturnsToLit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Lit -> eclipse -> lit: going back to the Sun side hides the ship light and gives channel 1 back to the sun light
bool FSOLShipSunlightReturnTest::RunTest(const FString& /*parameters*/)
{
    FSOLShipSunlightTestWorld testWorld;
    const int32 saturn = testWorld.FindBody(SOL::BodyNames::SATURN);
    if (!TestTrue(TEXT("Universe subsystems exist in the test world"), saturn != INDEX_NONE))
    {
        return false;
    }
    ASOLBodyVisuals* visuals = testWorld.World->SpawnActor<ASOLBodyVisuals>();
    if (!TestNotNull(TEXT("Body visuals spawned"), visuals))
    {
        return false;
    }

    testWorld.PlaceObserverAndUpdate(testWorld.AlongSunLine(saturn, 3.0));
    ShipSunExpectLit(*this, visuals, TEXT("First lit"));

    testWorld.PlaceObserverAndUpdate(testWorld.AlongSunLine(saturn, -3.0));
    ShipSunExpectShadowed(*this, visuals, visuals->GetShipSunTransmittance(), TEXT("Eclipse"));

    testWorld.PlaceObserverAndUpdate(testWorld.AlongSunLine(saturn, 3.0));
    TestTrue(FString::Printf(TEXT("T ~ 1 again (%.17g)"), visuals->GetShipSunTransmittance()),
        std::fabs(visuals->GetShipSunTransmittance() - 1.0) < SHIPSUN_LIT_TOL);
    ShipSunExpectLit(*this, visuals, TEXT("Back to lit"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLShipSunlightHysteresisTest, "SOLTest.ShipSunlight.Hysteresis",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// NextShipShadowedState enters the shadow below ENTER, leaves only at LEAVE or above, and keeps its state for NaN
bool FSOLShipSunlightHysteresisTest::RunTest(const FString& /*parameters*/)
{
    const double enter = SOL::SHIP_SUN_SHADOW_ENTER_BELOW;
    const double leave = SOL::SHIP_SUN_SHADOW_LEAVE_AT;
    const double between = 0.5 * (enter + leave);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("Lit stays lit at 1"), SOLRingShadow::NextShipShadowedState(false, 1.0));
    TestFalse(TEXT("Lit stays lit at ENTER"), SOLRingShadow::NextShipShadowedState(false, enter));
    TestFalse(TEXT("Lit stays lit between the thresholds"), SOLRingShadow::NextShipShadowedState(false, between));
    TestTrue(TEXT("Lit enters below ENTER"), SOLRingShadow::NextShipShadowedState(false, enter - 1.0e-9));
    TestTrue(TEXT("Lit enters at 0"), SOLRingShadow::NextShipShadowedState(false, 0.0));
    TestTrue(TEXT("Shadowed stays between the thresholds"), SOLRingShadow::NextShipShadowedState(true, between));
    TestTrue(TEXT("Shadowed stays just below LEAVE"), SOLRingShadow::NextShipShadowedState(true, leave - 1.0e-9));
    TestFalse(TEXT("Shadowed leaves at LEAVE"), SOLRingShadow::NextShipShadowedState(true, leave));
    TestFalse(TEXT("Shadowed leaves at 1"), SOLRingShadow::NextShipShadowedState(true, 1.0));
    TestTrue(TEXT("NaN keeps shadowed"), SOLRingShadow::NextShipShadowedState(true, nan));
    TestFalse(TEXT("NaN keeps lit"), SOLRingShadow::NextShipShadowedState(false, nan));

    // A fraction hovering between the thresholds never flips the state, whichever state it starts in
    bool bShadowed = true;
    for (int32 frame = 0; frame < 10; ++frame)
    {
        bShadowed = SOLRingShadow::NextShipShadowedState(bShadowed, frame % 2 == 0 ? enter : between);
    }
    TestTrue(TEXT("Hovering keeps the shadowed state"), bShadowed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLShipSunlightChannelsTest, "SOLTest.ShipSunlight.LightingChannelSetup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The ship's parts and the target ISM are on channel 1 only, and the ship's fill light lights channels 0 and 1
bool FSOLShipSunlightChannelsTest::RunTest(const FString& /*parameters*/)
{
    // Helper used by the ship's parts (AddShipPart) and the target ISM
    UStaticMeshComponent* part = NewObject<UStaticMeshComponent>();
    SOLShipSunlight::PutOnShipLightingChannel(part);
    TestTrue(TEXT("Ship part on channel 1 only"), !part->LightingChannels.bChannel0 &&
        part->LightingChannels.bChannel1 && !part->LightingChannels.bChannel2);
    SOLShipSunlight::PutOnShipLightingChannel(nullptr);   // must not crash

    // Defaults of the actors that own the target ISM and the fill light
    const ASOLCombatVisuals* combat = GetDefault<ASOLCombatVisuals>();
    const UInstancedStaticMeshComponent* targets = combat->FindComponentByClass<UInstancedStaticMeshComponent>();
    if (TestNotNull(TEXT("Combat visuals have the target ISM"), targets))
    {
        TestTrue(TEXT("Targets on channel 1 only"), !targets->LightingChannels.bChannel0 &&
            targets->LightingChannels.bChannel1 && !targets->LightingChannels.bChannel2);
    }
    const ASOLShipPawn* pawn = GetDefault<ASOLShipPawn>();
    const UDirectionalLightComponent* fill = pawn->FindComponentByClass<UDirectionalLightComponent>();
    if (TestNotNull(TEXT("Ship pawn has the fill light"), fill))
    {
        TestTrue(TEXT("Fill light on channels 0 and 1"), ShipSunHasChannels(fill, true, true, false));
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
