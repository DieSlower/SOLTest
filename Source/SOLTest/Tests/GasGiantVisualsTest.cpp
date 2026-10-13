/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "GasGiants/SOLGasGiantVisuals.h"

#include "SOLConstants.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Visuals/SOLBodyVisuals.h"

#include "Components/StaticMeshComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// World-level checks of ASOLGasGiantVisuals and ASOLBodyVisuals::SetBodyMeshHidden (SDD 14, 14c leftovers): a bare
// game world with the universe subsystems and play begun, so spawned actors run their real BeginPlay/EndPlay.
// Helpers here carry names unique across the test files so unity builds do not collide.
namespace
{
    // A temporary game world with begun play (no game mode, no player), destroyed at scope exit
    struct FSOLGasGiantTestWorld
    {
        UWorld* World = nullptr;

        //////////////////////////////////////////////////////////////////////////
        // Creates the world and its context and marks play as begun so spawned actors run BeginPlay
        FSOLGasGiantTestWorld()
        {
            World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SOLGasGiantVisualsTestWorld"));
            FWorldContext& context = GEngine->CreateNewWorldContext(EWorldType::Game);
            context.SetCurrentWorld(World);
            World->InitializeActorsForPlay(FURL());
            World->SetBegunPlay(true);
        }

        //////////////////////////////////////////////////////////////////////////
        // Ends play on every actor, then destroys the world and its context
        ~FSOLGasGiantTestWorld()
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
        // Spawns the body visuals (BeginPlay creates one placeholder mesh per body)
        ASOLBodyVisuals* SpawnBodyVisuals() const
        {
            return World->SpawnActor<ASOLBodyVisuals>();
        }

        //////////////////////////////////////////////////////////////////////////
        // Spawns a giant's visuals deferred, as ASOLGameMode does, with the planet name and body visuals set first
        ASOLGasGiantVisuals* SpawnGiant(const TCHAR* planetName, ASOLBodyVisuals* bodyVisuals) const
        {
            ASOLGasGiantVisuals* giant = World->SpawnActorDeferred<ASOLGasGiantVisuals>(
                ASOLGasGiantVisuals::StaticClass(), FTransform::Identity);
            giant->PlanetName = FName(planetName);
            giant->BodyVisuals = bodyVisuals;
            giant->FinishSpawning(FTransform::Identity);
            return giant;
        }

        //////////////////////////////////////////////////////////////////////////
        // Returns a body's registry index, or INDEX_NONE without the registry
        int32 FindBody(const TCHAR* name) const
        {
            const USOLBodyRegistrySubsystem* registry = World->GetSubsystem<USOLBodyRegistrySubsystem>();
            return registry != nullptr ? registry->GetRegistry().FindByName(FName(name)) : INDEX_NONE;
        }

        //////////////////////////////////////////////////////////////////////////
        // Places the observer at a multiple of a body's radius from its centre and runs one universe update
        // (the giants' only update path) with the given real frame time
        void PlaceObserverAndUpdate(const int32 bodyIndex, const double radii, const double realDeltaSeconds) const
        {
            USOLAnchorSubsystem* anchor = World->GetSubsystem<USOLAnchorSubsystem>();
            const FSOLBodyRegistry& registry = World->GetSubsystem<USOLBodyRegistrySubsystem>()->GetRegistry();
            anchor->SetObserverPositionM(registry.GetPositionM(bodyIndex) +
                FVector3d(radii * registry.GetRadiusM(bodyIndex), 0.0, 0.0));
            FApp::SetDeltaTime(realDeltaSeconds);
            anchor->OnUniverseUpdated().Broadcast();
        }
    };

    // Restores FApp's delta time at scope exit (the tests drive the release timer through it)
    struct FSOLGasGiantDeltaTimeGuard
    {
        double Saved = FApp::GetDeltaTime();

        //////////////////////////////////////////////////////////////////////////
        // Puts the saved delta time back
        ~FSOLGasGiantDeltaTimeGuard()
        {
            FApp::SetDeltaTime(Saved);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantSetBodyMeshHiddenTest, "SOLTest.GasGiantVisuals.SetBodyMeshHidden",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SetBodyMeshHidden hides and shows a valid body's placeholder mesh and rejects invalid indices
bool FSOLGasGiantSetBodyMeshHiddenTest::RunTest(const FString& /*parameters*/)
{
    FSOLGasGiantTestWorld testWorld;
    const int32 mars = testWorld.FindBody(SOL::BodyNames::MARS);
    if (!TestTrue(TEXT("Universe subsystems exist in the test world"), mars != INDEX_NONE))
    {
        return false;
    }
    ASOLBodyVisuals* bodyVisuals = testWorld.SpawnBodyVisuals();
    if (!TestNotNull(TEXT("Body visuals spawned"), bodyVisuals))
    {
        return false;
    }

    TestFalse(TEXT("Visible after BeginPlay"), bodyVisuals->IsBodyMeshHidden(mars));
    TestTrue(TEXT("Hide a valid body"), bodyVisuals->SetBodyMeshHidden(mars, true));
    TestTrue(TEXT("Now hidden"), bodyVisuals->IsBodyMeshHidden(mars));
    TestTrue(TEXT("Hiding twice is fine"), bodyVisuals->SetBodyMeshHidden(mars, true));
    TestTrue(TEXT("Show it again"), bodyVisuals->SetBodyMeshHidden(mars, false));
    TestFalse(TEXT("Visible again"), bodyVisuals->IsBodyMeshHidden(mars));
    TestFalse(TEXT("Negative index rejected"), bodyVisuals->SetBodyMeshHidden(-1, true));
    TestFalse(TEXT("Out-of-range index rejected"), bodyVisuals->SetBodyMeshHidden(100000, true));
    TestFalse(TEXT("Out-of-range index is not hidden"), bodyVisuals->IsBodyMeshHidden(100000));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantVisualsLifecycleTest,
    "SOLTest.GasGiantVisuals.ReplacesAndRestoresBodyMesh",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A giant's visuals hide its placeholder body mesh (and only its own), draw the deck at the body's place, keep the
// near layer released while far away, and show the placeholder again when destroyed
bool FSOLGasGiantVisualsLifecycleTest::RunTest(const FString& /*parameters*/)
{
    FSOLGasGiantTestWorld testWorld;
    const int32 jupiter = testWorld.FindBody(SOL::BodyNames::JUPITER);
    const int32 saturn = testWorld.FindBody(SOL::BodyNames::SATURN);
    if (!TestTrue(TEXT("Universe subsystems exist in the test world"), jupiter != INDEX_NONE && saturn != INDEX_NONE))
    {
        return false;
    }
    ASOLBodyVisuals* bodyVisuals = testWorld.SpawnBodyVisuals();
    ASOLGasGiantVisuals* giant = testWorld.SpawnGiant(SOL::BodyNames::JUPITER, bodyVisuals);
    if (!TestNotNull(TEXT("Giant visuals spawned"), giant))
    {
        return false;
    }

    TestTrue(TEXT("Jupiter's placeholder mesh is hidden"), bodyVisuals->IsBodyMeshHidden(jupiter));
    TestFalse(TEXT("Saturn's placeholder mesh is untouched"), bodyVisuals->IsBodyMeshHidden(saturn));
    UStaticMeshComponent* deck = giant->GetDeckMesh();
    TestTrue(TEXT("Deck has the sphere mesh"), deck != nullptr && deck->GetStaticMesh() != nullptr);
    TestTrue(TEXT("Deck has a material"), deck != nullptr && deck->GetMaterial(0) != nullptr);
    TestTrue(TEXT("Deck is visible"), deck != nullptr && deck->IsVisible());
    const USOLAnchorSubsystem* anchor = testWorld.World->GetSubsystem<USOLAnchorSubsystem>();
    const double expectedScale = anchor->ComputeBodyRenderPlacement(jupiter).RadiusCm / SOL::BODY_MESH_RADIUS_CM;
    TestTrue(TEXT("Deck is scaled to the body's render radius"), deck != nullptr &&
        FMath::IsNearlyEqual(deck->GetComponentScale().X, expectedScale, expectedScale * 1.0e-5));
    TestFalse(TEXT("Near layer released while the observer is far away"), giant->IsNearLayerActive());

    giant->Destroy();
    TestFalse(TEXT("Placeholder shown again after the giant visuals end"), bodyVisuals->IsBodyMeshHidden(jupiter));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantVisualsUnknownTest, "SOLTest.GasGiantVisuals.UnknownPlanetIsInert",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Visuals for a name that is not a giant (or no body visuals at all) hide nothing and do not crash
bool FSOLGasGiantVisualsUnknownTest::RunTest(const FString& /*parameters*/)
{
    FSOLGasGiantTestWorld testWorld;
    const int32 mars = testWorld.FindBody(SOL::BodyNames::MARS);
    ASOLBodyVisuals* bodyVisuals = testWorld.SpawnBodyVisuals();
    AddExpectedError(TEXT("no body or appearance"), EAutomationExpectedErrorFlags::Contains, 1);
    ASOLGasGiantVisuals* notAGiant = testWorld.SpawnGiant(SOL::BodyNames::MARS, bodyVisuals);
    TestNotNull(TEXT("Spawned"), notAGiant);
    TestFalse(TEXT("Mars is not hidden"), bodyVisuals->IsBodyMeshHidden(mars));
    ASOLGasGiantVisuals* orphan = testWorld.SpawnGiant(SOL::BodyNames::NEPTUNE, nullptr);
    TestNotNull(TEXT("Spawned without body visuals"), orphan);
    TestFalse(TEXT("Its near layer is released"), orphan != nullptr && orphan->IsNearLayerActive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantSunAtmosphereTest,
    "SOLTest.GasGiantVisuals.SunDrivesAtmosphereRequesters",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The atmosphere-sun flag stays on while any requester holds it; releasing an unknown requester changes nothing
bool FSOLGasGiantSunAtmosphereTest::RunTest(const FString& /*parameters*/)
{
    FSOLGasGiantTestWorld testWorld;
    ASOLBodyVisuals* bodyVisuals = testWorld.SpawnBodyVisuals();
    if (!TestNotNull(TEXT("Body visuals spawned"), bodyVisuals))
    {
        return false;
    }
    const UObject* first = ASOLGasGiantVisuals::StaticClass();
    const UObject* second = ASOLBodyVisuals::StaticClass();
    TestFalse(TEXT("Off at start"), bodyVisuals->IsSunDrivingAtmosphere());
    bodyVisuals->SetSunDrivesAtmosphere(first, true);
    TestTrue(TEXT("On with one requester"), bodyVisuals->IsSunDrivingAtmosphere());
    bodyVisuals->SetSunDrivesAtmosphere(second, true);
    bodyVisuals->SetSunDrivesAtmosphere(first, false);
    TestTrue(TEXT("Still on while the second holds it"), bodyVisuals->IsSunDrivingAtmosphere());
    bodyVisuals->SetSunDrivesAtmosphere(first, false);
    TestTrue(TEXT("Releasing twice does not clear the other's hold"), bodyVisuals->IsSunDrivingAtmosphere());
    bodyVisuals->SetSunDrivesAtmosphere(second, false);
    TestFalse(TEXT("Off once nobody holds it"), bodyVisuals->IsSunDrivingAtmosphere());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGasGiantNearLayerTest, "SOLTest.GasGiantVisuals.NearLayerLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Near the giant the volumetric layer is created and shown, the haze hidden and the sun flagged; far away it hides
// and, after the release delay, is destroyed; coming back re-creates it (fresh components, right after the release,
// before any garbage collection); EndPlay clears the sun flag
bool FSOLGasGiantNearLayerTest::RunTest(const FString& /*parameters*/)
{
    FSOLGasGiantTestWorld testWorld;
    FSOLGasGiantDeltaTimeGuard deltaGuard;
    const int32 jupiter = testWorld.FindBody(SOL::BodyNames::JUPITER);
    if (!TestTrue(TEXT("Universe subsystems exist in the test world"), jupiter != INDEX_NONE))
    {
        return false;
    }
    ASOLBodyVisuals* bodyVisuals = testWorld.SpawnBodyVisuals();
    ASOLGasGiantVisuals* giant = testWorld.SpawnGiant(SOL::BodyNames::JUPITER, bodyVisuals);
    if (!TestNotNull(TEXT("Giant visuals spawned"), giant))
    {
        return false;
    }
    const double frame = 1.0 / 60.0;
    const double nearRadii = 0.5 * (1.0 + SOL::GAS_GIANT_NEAR_FADE_IN_RADII);
    const double farRadii = 2.0 * SOL::GAS_GIANT_NEAR_FADE_OUT_RADII;

    // Within FADE_IN radii: fully shown
    testWorld.PlaceObserverAndUpdate(jupiter, nearRadii, frame);
    TestTrue(TEXT("Near: layer active"), giant->IsNearLayerActive());
    TestTrue(TEXT("Near: layer shown"), giant->IsNearLayerShown());
    UVolumetricCloudComponent* firstClouds = giant->GetNearClouds();
    TestTrue(TEXT("Near: cloud component exists, registered and visible"), firstClouds != nullptr &&
        firstClouds->IsRegistered() && firstClouds->IsVisible());
    TestTrue(TEXT("Near: sun drives the atmosphere"), bodyVisuals->IsSunDrivingAtmosphere());
    TestFalse(TEXT("Near: haze hidden at blend 1"), giant->GetHazeMesh()->IsVisible());

    // Beyond FADE_OUT radii: hidden but still resident until the delay passes
    testWorld.PlaceObserverAndUpdate(jupiter, farRadii, frame);
    TestTrue(TEXT("Far: still resident"), giant->IsNearLayerActive());
    TestFalse(TEXT("Far: not shown"), giant->IsNearLayerShown());
    TestFalse(TEXT("Far: sun no longer drives the atmosphere"), bodyVisuals->IsSunDrivingAtmosphere());
    TestTrue(TEXT("Far: haze visible again"), giant->GetHazeMesh()->IsVisible());
    testWorld.PlaceObserverAndUpdate(jupiter, farRadii, SOL::GAS_GIANT_NEAR_RELEASE_DELAY_SECONDS + 1.0);
    TestFalse(TEXT("After the delay: released"), giant->IsNearLayerActive());
    TestNull(TEXT("After the delay: no cloud component"), giant->GetNearClouds());

    // Back near straight away (the destroyed components are not collected yet): fresh components
    testWorld.PlaceObserverAndUpdate(jupiter, nearRadii, frame);
    UVolumetricCloudComponent* secondClouds = giant->GetNearClouds();
    TestTrue(TEXT("Re-activated"), giant->IsNearLayerActive() && giant->IsNearLayerShown());
    TestTrue(TEXT("Re-activated with a new registered component"), secondClouds != nullptr &&
        secondClouds != firstClouds && secondClouds->IsRegistered() && secondClouds->IsVisible());
    TestTrue(TEXT("Re-activated: sun drives the atmosphere"), bodyVisuals->IsSunDrivingAtmosphere());

    giant->Destroy();
    TestFalse(TEXT("EndPlay clears the sun flag"), bodyVisuals->IsSunDrivingAtmosphere());
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
