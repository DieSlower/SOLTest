/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "SOLConstants.h"
#include "Visuals/SOLThrusterMath.h"
#include "Visuals/SOLThrusterRibbonComponent.h"
#include "Visuals/SOLThrusterRibbons.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// World-level checks of ASOLThrusterRibbons' shared flame store (SDD 51 R7, R10-Q4, R10-8): the spool-down bookkeeping
// of a committed feed, release and slot reuse, the pending-feed commit order, the resume-frame fall-back to the strip
// pool, the player's exemption from the pixel rule and the strip pool's instanced blocks. A bare game world with play
// begun and a player controller (camera at the origin, no viewport: no frustum, no device focal), driven frame by frame
// with the verification clock; the ribbons are ticked directly. Helpers carry names unique across the test files so
// unity builds do not collide.
namespace
{
    constexpr float THRSTORE_TOL = 1.0e-5f;                     // Header texels are float
    constexpr float THRSTORE_DT_S = 1.0f / 120.0f;              // Tick delta (unused by the store: it reads the clock)
    constexpr double THRSTORE_NEAR_M = 100.0;                   // Far above the vanish length on screen
    constexpr double THRSTORE_FAR_M = 1.0e9;                    // Far below it
    constexpr double THRSTORE_PLUME_M = 10.0;                   // L_p of a burning feed
    constexpr float THRSTORE_NOZZLE_M = 0.5f;
    constexpr float THRSTORE_ALL_POOL_PX = 1.0e9f;              // Niagara threshold sending every NPC flame to the pool
    constexpr int32 THRSTORE_HEADER_TEXEL = SOL::Thruster::FLAME_DRAWN_POINTS;  // (c, b, L_p, r_n) of pool trail 0

    // A temporary game world with begun play, a player controller and the ribbons actor; the two ribbon cvars are
    // restored and the world destroyed at scope exit
    struct FSOLThrusterStoreWorld
    {
        UWorld* World = nullptr;
        ASOLThrusterRibbons* Ribbons = nullptr;
        IConsoleVariable* RibbonsCVar = nullptr;
        IConsoleVariable* NiagaraPxCVar = nullptr;
        int32 SavedRibbons = 1;
        float SavedNiagaraPx = -1.0f;

        //////////////////////////////////////////////////////////////////////////
        // Creates the world, begins play, spawns the controller (its camera manager) and the ribbons, turns them on
        FSOLThrusterStoreWorld()
        {
            RibbonsCVar = IConsoleManager::Get().FindConsoleVariable(SOL::THRUSTER_RIBBONS_CVAR);
            NiagaraPxCVar = IConsoleManager::Get().FindConsoleVariable(SOL::THRUSTER_RIBBON_NIAGARA_PX_CVAR);
            if (RibbonsCVar != nullptr)
            {
                SavedRibbons = RibbonsCVar->GetInt();
                RibbonsCVar->Set(1, ECVF_SetByCode);
            }
            if (NiagaraPxCVar != nullptr)
            {
                SavedNiagaraPx = NiagaraPxCVar->GetFloat();
            }
            World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SOLThrusterStoreTestWorld"));
            FWorldContext& context = GEngine->CreateNewWorldContext(EWorldType::Game);
            context.SetCurrentWorld(World);
            World->InitializeActorsForPlay(FURL());
            World->SetBegunPlay(true);
            World->SpawnActor<APlayerController>();
            Ribbons = World->SpawnActor<ASOLThrusterRibbons>();
        }

        //////////////////////////////////////////////////////////////////////////
        // Restores the cvars and the clock, ends play on every actor, then destroys the world and its context
        ~FSOLThrusterStoreWorld()
        {
            if (Ribbons != nullptr)
            {
                Ribbons->SetVerificationClock(-1.0);
            }
            if (RibbonsCVar != nullptr)
            {
                RibbonsCVar->Set(SavedRibbons, ECVF_SetByCode);
            }
            if (NiagaraPxCVar != nullptr)
            {
                NiagaraPxCVar->Set(SavedNiagaraPx, ECVF_SetByCode);
            }
            for (TActorIterator<AActor> it(World); it; ++it)
            {
                it->RouteEndPlay(EEndPlayReason::Quit);
            }
            World->SetBegunPlay(false);
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }

        //////////////////////////////////////////////////////////////////////////
        // Sets the on-screen length from which an NPC flame is drawn by Niagara
        void SetNiagaraPx(const float px) const
        {
            if (NiagaraPxCVar != nullptr)
            {
                NiagaraPxCVar->Set(px, ECVF_SetByCode);
            }
        }

        //////////////////////////////////////////////////////////////////////////
        // Runs one frame at nowS: a new frame number, the NPC feeds through FeedNpc and CommitNpcFeeds as the placement
        // pass makes them, the player's nozzle 0 when given, then the ribbons' render
        void Frame(const double nowS, TConstArrayView<FSOLRibbonFeed> npcFeeds,
            const FSOLRibbonFeed* playerFeed = nullptr) const
        {
            ++GFrameCounter;
            Ribbons->SetVerificationClock(nowS);
            const TArrayView<FSOLRibbonFeed> slots = Ribbons->BeginNpcFeeds(npcFeeds.Num());
            for (int32 index = 0; index < npcFeeds.Num(); ++index)
            {
                Ribbons->FeedNpc(Ribbons->FindTrail(npcFeeds[index].Key), npcFeeds[index], slots[index]);
            }
            Ribbons->CommitNpcFeeds();
            if (playerFeed != nullptr)
            {
                Ribbons->FeedPlayer(0, *playerFeed);
            }
            Ribbons->Tick(THRSTORE_DT_S);
        }

        //////////////////////////////////////////////////////////////////////////
        // Returns the strip pool's header texel of its first trail, (c, b, L_p, r_n); zero without one
        FVector4f PoolHeader() const
        {
            const USOLThrusterRibbonComponent* pool = Ribbons->GetStripPool();
            const TConstArrayView<FVector4f> texels = pool != nullptr ? pool->GetLastTexels()
                : TConstArrayView<FVector4f>();
            return texels.IsValidIndex(THRSTORE_HEADER_TEXEL) ? texels[THRSTORE_HEADER_TEXEL]
                : FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
        }

        //////////////////////////////////////////////////////////////////////////
        // Returns the strip pool's instanced blocks; -1 without the pool
        int32 PoolBlocks() const
        {
            const USOLThrusterRibbonComponent* pool = Ribbons->GetStripPool();
            return pool != nullptr ? pool->GetBlockCapacity() : -1;
        }
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns a feed of a ship distanceM ahead of the camera: burning at b with L_p and c, or off for b = 0
    FSOLRibbonFeed ThrStoreFeed(const int32 key, const double distanceM, const float brightness,
        const double plumeLengthM = THRSTORE_PLUME_M, const float colour = 0.5f)
    {
        FSOLRibbonFeed feed;
        feed.Key = key;
        feed.ShipPositionM = FVector3d(distanceM, 0.0, 0.0);
        feed.DistanceM = distanceM;
        feed.Pose.OffsetM = FVector3f(-5.0f, 0.0f, 0.0f);
        feed.Pose.ExhaustDir = FVector3f(-1.0f, 0.0f, 0.0f);
        feed.Pose.Brightness = brightness;
        feed.PlumeLengthM = brightness > 0.0f ? plumeLengthM : 0.0;
        feed.ColourScalar = colour;
        feed.NozzleRadiusM = THRSTORE_NOZZLE_M;
        return feed;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the drawn NPC trails of last frame
    TArray<FSOLRibbonDrawnTrail> ThrStoreDrawn(const ASOLThrusterRibbons& ribbons)
    {
        TArray<FSOLRibbonDrawnTrail> drawn;
        ribbons.GetDrawnNpcTrails(drawn);
        return drawn;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterStoreSpoolDownTest, "SOLTest.Thruster.Store.SpoolDown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// R10-Q4 through CommitFeedToSlot: once the thrust cuts out, b falls linearly from its last burning value to 0 over
// FLAME_SHUTDOWN_S with L_p and c held; a new burn clears the cut-off, so the next one starts from the new b at its
// own time; the trail lives on at b = 0 until its gas has left (the flame life after the last b > 0), then is released
bool FSOLThrusterStoreSpoolDownTest::RunTest(const FString& /*parameters*/)
{
    FSOLThrusterStoreWorld world;
    if (!TestNotNull(TEXT("ribbons spawned"), world.Ribbons))
    {
        return false;
    }
    world.SetNiagaraPx(THRSTORE_ALL_POOL_PX);
    constexpr int32 key = 7;
    constexpr double shutdownS = SOL::Thruster::FLAME_SHUTDOWN_S;

    // Burning: the header carries this frame's values
    world.Frame(1.0, { ThrStoreFeed(key, THRSTORE_NEAR_M, 0.8f, THRSTORE_PLUME_M, 0.6f) });
    TestEqual(TEXT("burning: drawn by the pool"), world.Ribbons->GetPoolTrailCount(), 1);
    FVector4f header = world.PoolHeader();
    TestEqual(TEXT("burning: c"), header.X, 0.6f, THRSTORE_TOL);
    TestEqual(TEXT("burning: b"), header.Y, 0.8f, THRSTORE_TOL);
    TestEqual(TEXT("burning: L_p"), header.Z, static_cast<float>(THRSTORE_PLUME_M), THRSTORE_TOL);
    TestEqual(TEXT("burning: r_n"), header.W, THRSTORE_NOZZLE_M, THRSTORE_TOL);

    // Cut-off at 1.5 s (the feed has L_p 0, c 0.1): b still 0.8 at the cut-off, L_p and c held
    world.Frame(1.5, { ThrStoreFeed(key, THRSTORE_NEAR_M, 0.0f, 0.0, 0.1f) });
    header = world.PoolHeader();
    TestEqual(TEXT("cut-off: b starts at the last burning b"), header.Y, 0.8f, THRSTORE_TOL);
    TestEqual(TEXT("cut-off: c held"), header.X, 0.6f, THRSTORE_TOL);
    TestEqual(TEXT("cut-off: L_p held"), header.Z, static_cast<float>(THRSTORE_PLUME_M), THRSTORE_TOL);

    // Half way down: b 0.4 (measured from the cut-off, not from the later feeds)
    world.Frame(1.5 + 0.25 * shutdownS, { ThrStoreFeed(key, THRSTORE_NEAR_M, 0.0f) });
    world.Frame(1.5 + 0.5 * shutdownS, { ThrStoreFeed(key, THRSTORE_NEAR_M, 0.0f) });
    TestEqual(TEXT("half way: b 0.4"), world.PoolHeader().Y, 0.4f, THRSTORE_TOL);

    // A new burn (b 0.9, L_p 12) clears the cut-off; the next cut-off starts at 0.9 at its own time
    world.Frame(2.0, { ThrStoreFeed(key, THRSTORE_NEAR_M, 0.9f, 12.0, 0.3f) });
    world.Frame(2.1, { ThrStoreFeed(key, THRSTORE_NEAR_M, 0.0f) });
    header = world.PoolHeader();
    TestEqual(TEXT("second cut-off: b starts at 0.9"), header.Y, 0.9f, THRSTORE_TOL);
    TestEqual(TEXT("second cut-off: L_p held at 12"), header.Z, 12.0f, THRSTORE_TOL);
    TestEqual(TEXT("second cut-off: c held at 0.3"), header.X, 0.3f, THRSTORE_TOL);
    world.Frame(2.1 + 0.75 * shutdownS, { ThrStoreFeed(key, THRSTORE_NEAR_M, 0.0f) });
    TestEqual(TEXT("second cut-off, 3/4 down: b 0.225"), world.PoolHeader().Y, 0.225f, THRSTORE_TOL);

    // Just before the end (b > 0 for the last time), then just after it: b 0, the trail still holds gas
    const double flameLifeS = SOLThruster::FlameLengthM(12.0) / SOL::Thruster::FLAME_EXHAUST_SPEED_MPS;
    const double lastLitS = 2.1 + 0.99 * shutdownS;
    world.Frame(lastLitS, { ThrStoreFeed(key, THRSTORE_NEAR_M, 0.0f) });
    TestTrue(TEXT("last lit frame: b > 0"), world.PoolHeader().Y > 0.0f);
    world.Frame(2.1 + shutdownS + 0.1 * flameLifeS, { ThrStoreFeed(key, THRSTORE_NEAR_M, 0.0f) });
    TestEqual(TEXT("spooled down: b 0"), world.PoolHeader().Y, 0.0f, THRSTORE_TOL);
    TestTrue(TEXT("spooled down: still live while its gas is in the flame"),
        world.Ribbons->FindTrail(key) != INDEX_NONE);

    // Its gas has left one flame life after the last lit pose: released though still fed
    world.Frame(lastLitS + 1.01 * flameLifeS, { ThrStoreFeed(key, THRSTORE_NEAR_M, 0.0f) });
    TestEqual(TEXT("gas gone: released"), world.Ribbons->FindTrail(key), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("gas gone: no live trail"), world.Ribbons->GetLiveTrailCount(), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterStoreReleaseAcquireTest, "SOLTest.Thruster.Store.ReleaseAcquire",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A trail is created only for a burning ship; a trail left unfed is kept while its last pixel fade was above 0 and
// released the first unfed frame after a fade of 0; a released slot is reused by the next new trail
bool FSOLThrusterStoreReleaseAcquireTest::RunTest(const FString& /*parameters*/)
{
    FSOLThrusterStoreWorld world;
    if (!TestNotNull(TEXT("ribbons spawned"), world.Ribbons))
    {
        return false;
    }
    world.SetNiagaraPx(THRSTORE_ALL_POOL_PX);

    // Two burning ships and one that does not burn
    world.Frame(1.0, { ThrStoreFeed(10, THRSTORE_NEAR_M, 1.0f), ThrStoreFeed(11, THRSTORE_NEAR_M, 1.0f),
        ThrStoreFeed(13, THRSTORE_NEAR_M, 0.0f) });
    const int32 slot10 = world.Ribbons->FindTrail(10);
    const int32 slot11 = world.Ribbons->FindTrail(11);
    TestTrue(TEXT("burning ships get trails"), slot10 != INDEX_NONE && slot11 != INDEX_NONE && slot10 != slot11);
    TestEqual(TEXT("a dark ship gets none"), world.Ribbons->FindTrail(13), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("two live"), world.Ribbons->GetLiveTrailCount(), 2);
    TestEqual(TEXT("two drawn"), world.Ribbons->GetPoolTrailCount(), 2);

    // 11 unfed with its last fade above 0: kept (not drawn)
    world.Frame(1.002, { ThrStoreFeed(10, THRSTORE_NEAR_M, 1.0f) });
    TestEqual(TEXT("unfed, fade > 0: kept"), world.Ribbons->FindTrail(11), slot11);
    TestEqual(TEXT("unfed: not drawn"), world.Ribbons->GetPoolTrailCount(), 1);

    // 11 fed far below the vanish length (fade 0): kept that frame, not drawn
    world.Frame(1.004, { ThrStoreFeed(10, THRSTORE_NEAR_M, 1.0f), ThrStoreFeed(11, THRSTORE_FAR_M, 1.0f) });
    TestEqual(TEXT("fed, under the pixel rule: kept"), world.Ribbons->FindTrail(11), slot11);
    TestEqual(TEXT("under the pixel rule: not drawn"), world.Ribbons->GetPoolTrailCount(), 1);

    // 11 unfed after a fade of 0: released at once
    world.Frame(1.006, { ThrStoreFeed(10, THRSTORE_NEAR_M, 1.0f) });
    TestEqual(TEXT("unfed after fade 0: released"), world.Ribbons->FindTrail(11), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("one live"), world.Ribbons->GetLiveTrailCount(), 1);
    TestEqual(TEXT("10 untouched"), world.Ribbons->FindTrail(10), slot10);

    // A new burning ship reuses the freed slot
    world.Frame(1.008, { ThrStoreFeed(10, THRSTORE_NEAR_M, 1.0f), ThrStoreFeed(12, THRSTORE_NEAR_M, 1.0f) });
    TestEqual(TEXT("new trail takes the freed slot"), world.Ribbons->FindTrail(12), slot11);
    TestEqual(TEXT("two live again"), world.Ribbons->GetLiveTrailCount(), 2);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterStorePendingOrderTest, "SOLTest.Thruster.Store.PendingOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// CommitNpcFeeds creates the new trails of the feeds FeedNpc listed, in feed-slot order whatever order the (parallel)
// feeders listed them in, and visits only listed slots: a slot written directly is never committed
bool FSOLThrusterStorePendingOrderTest::RunTest(const FString& /*parameters*/)
{
    FSOLThrusterStoreWorld world;
    if (!TestNotNull(TEXT("ribbons spawned"), world.Ribbons))
    {
        return false;
    }
    ++GFrameCounter;
    world.Ribbons->SetVerificationClock(1.0);
    const TArrayView<FSOLRibbonFeed> slots = world.Ribbons->BeginNpcFeeds(5);

    // Listed out of order: slot 3, 1, 2; slot 4 is dark (not listed); slot 0 is written directly (not listed)
    world.Ribbons->FeedNpc(INDEX_NONE, ThrStoreFeed(30, THRSTORE_NEAR_M, 1.0f), slots[3]);
    world.Ribbons->FeedNpc(INDEX_NONE, ThrStoreFeed(31, THRSTORE_NEAR_M, 1.0f), slots[1]);
    world.Ribbons->FeedNpc(INDEX_NONE, ThrStoreFeed(32, THRSTORE_NEAR_M, 1.0f), slots[2]);
    world.Ribbons->FeedNpc(INDEX_NONE, ThrStoreFeed(33, THRSTORE_NEAR_M, 0.0f), slots[4]);
    slots[0] = ThrStoreFeed(34, THRSTORE_NEAR_M, 1.0f);
    slots[0].bValid = true;
    TestEqual(TEXT("before the commit: no trail"), world.Ribbons->FindTrail(31), static_cast<int32>(INDEX_NONE));
    world.Ribbons->CommitNpcFeeds();

    // A fresh store hands out slots 0, 1, 2 in commit order
    TestEqual(TEXT("feed slot 1 committed first"), world.Ribbons->FindTrail(31), 0);
    TestEqual(TEXT("feed slot 2 committed second"), world.Ribbons->FindTrail(32), 1);
    TestEqual(TEXT("feed slot 3 committed third"), world.Ribbons->FindTrail(30), 2);
    TestEqual(TEXT("a dark feed is not listed"), world.Ribbons->FindTrail(33), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("an unlisted slot is not committed"), world.Ribbons->FindTrail(34), static_cast<int32>(INDEX_NONE));

    // The list is consumed: a second commit creates nothing more
    world.Ribbons->CommitNpcFeeds();
    world.Ribbons->Tick(THRSTORE_DT_S);
    TestEqual(TEXT("three live"), world.Ribbons->GetLiveTrailCount(), 3);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterStoreResumeFallbackTest, "SOLTest.Thruster.Store.ResumeFallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A flame given a parked Niagara component is drawn by the strip pool on that component's resume frame and by Niagara
// from the next frame; the drawn-trail report labels each NPC flame with the renderer that drew it and never lists
// the player's trail as an NPC one
bool FSOLThrusterStoreResumeFallbackTest::RunTest(const FString& /*parameters*/)
{
    FSOLThrusterStoreWorld world;
    if (!TestNotNull(TEXT("ribbons spawned"), world.Ribbons))
    {
        return false;
    }
    world.SetNiagaraPx(0.0f);
    constexpr int32 npc = 20;
    const FSOLRibbonFeed player = ThrStoreFeed(INDEX_NONE, THRSTORE_NEAR_M, 1.0f);

    // Resume frame: the player's and the NPC's components were parked, so the pool draws both
    world.Frame(1.0, { ThrStoreFeed(npc, THRSTORE_NEAR_M, 1.0f) }, &player);
    TestEqual(TEXT("resume frame: Niagara draws none"), world.Ribbons->GetNiagaraTrailCount(), 0);
    TestEqual(TEXT("resume frame: the pool draws both"), world.Ribbons->GetPoolTrailCount(), 2);
    TArray<FSOLRibbonDrawnTrail> drawn = ThrStoreDrawn(*world.Ribbons);
    TestEqual(TEXT("resume frame: one NPC entry (the player is not one)"), drawn.Num(), 1);
    if (drawn.Num() == 1)
    {
        TestEqual(TEXT("resume frame: the NPC's key"), drawn[0].Key, npc);
        TestFalse(TEXT("resume frame: labelled pool"), drawn[0].bNiagara);
    }

    // Next frame: both drawn by Niagara, the NPC labelled so
    world.Frame(1.002, { ThrStoreFeed(npc, THRSTORE_NEAR_M, 1.0f) }, &player);
    TestEqual(TEXT("next frame: Niagara draws both"), world.Ribbons->GetNiagaraTrailCount(), 2);
    TestEqual(TEXT("next frame: the pool draws none"), world.Ribbons->GetPoolTrailCount(), 0);
    TestEqual(TEXT("next frame: two active components"), world.Ribbons->GetActiveNiagaraCount(), 2);
    drawn = ThrStoreDrawn(*world.Ribbons);
    TestEqual(TEXT("next frame: one NPC entry"), drawn.Num(), 1);
    if (drawn.Num() == 1)
    {
        TestEqual(TEXT("next frame: the NPC's key"), drawn[0].Key, npc);
        TestTrue(TEXT("next frame: labelled Niagara"), drawn[0].bNiagara);
    }

    // A second, farther NPC joins: components go by pixel length, so the first keeps its active component and the
    // newcomer's resumes (the pool's this frame)
    constexpr int32 npc2 = 21;
    world.Frame(1.004, { ThrStoreFeed(npc, THRSTORE_NEAR_M, 1.0f), ThrStoreFeed(npc2, 2.0 * THRSTORE_NEAR_M, 1.0f) },
        &player);
    TestEqual(TEXT("newcomer frame: Niagara draws the player and one NPC"), world.Ribbons->GetNiagaraTrailCount(), 2);
    TestEqual(TEXT("newcomer frame: the pool draws the newcomer"), world.Ribbons->GetPoolTrailCount(), 1);
    drawn = ThrStoreDrawn(*world.Ribbons);
    TestEqual(TEXT("newcomer frame: two NPC entries"), drawn.Num(), 2);
    const FSOLRibbonDrawnTrail* first = drawn.FindByPredicate([](const FSOLRibbonDrawnTrail& trail)
    {
        return trail.Key == npc;
    });
    const FSOLRibbonDrawnTrail* second = drawn.FindByPredicate([](const FSOLRibbonDrawnTrail& trail)
    {
        return trail.Key == npc2;
    });
    TestTrue(TEXT("newcomer frame: the first NPC labelled Niagara"), first != nullptr && first->bNiagara);
    TestTrue(TEXT("newcomer frame: the newcomer labelled pool"), second != nullptr && !second->bNiagara);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterStorePlayerExemptionTest, "SOLTest.Thruster.Store.PlayerExemption",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// R5: the player's flame is never gated by the pixel rule. Fed with the same pose far below the vanish length, the
// player's trail is drawn and the NPC's is not; left unfed, the NPC's (fade 0) is released at once while the player's
// (fade 1) is kept until its gas has left
bool FSOLThrusterStorePlayerExemptionTest::RunTest(const FString& /*parameters*/)
{
    FSOLThrusterStoreWorld world;
    if (!TestNotNull(TEXT("ribbons spawned"), world.Ribbons))
    {
        return false;
    }
    world.SetNiagaraPx(THRSTORE_ALL_POOL_PX);
    constexpr int32 npc = 50;
    const int32 playerKey = ASOLThrusterRibbons::PlayerRibbonKey(0);
    const FSOLRibbonFeed player = ThrStoreFeed(INDEX_NONE, THRSTORE_FAR_M, 1.0f);

    world.Frame(1.0, { ThrStoreFeed(npc, THRSTORE_FAR_M, 1.0f) }, &player);
    TestEqual(TEXT("far: both live"), world.Ribbons->GetLiveTrailCount(), 2);
    TestEqual(TEXT("far: only the player's flame is drawn"),
        world.Ribbons->GetNiagaraTrailCount() + world.Ribbons->GetPoolTrailCount(), 1);
    TestEqual(TEXT("far: no NPC flame drawn"), ThrStoreDrawn(*world.Ribbons).Num(), 0);

    // Unfed: the NPC's released, the player's kept while its gas is in the flame
    world.Frame(1.002, {});
    TestEqual(TEXT("unfed: the NPC's released"), world.Ribbons->FindTrail(npc), static_cast<int32>(INDEX_NONE));
    TestTrue(TEXT("unfed: the player's kept"), world.Ribbons->FindTrail(playerKey) != INDEX_NONE);
    TestEqual(TEXT("unfed: the player's not drawn"),
        world.Ribbons->GetNiagaraTrailCount() + world.Ribbons->GetPoolTrailCount(), 0);

    // Released once its gas has left
    const double flameLifeS = SOLThruster::FlameLengthM(THRSTORE_PLUME_M) / SOL::Thruster::FLAME_EXHAUST_SPEED_MPS;
    world.Frame(1.0 + 1.01 * flameLifeS, {});
    TestEqual(TEXT("gas gone: the player's released"), world.Ribbons->FindTrail(playerKey),
        static_cast<int32>(INDEX_NONE));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLThrusterStorePoolBlocksTest, "SOLTest.Thruster.Store.PoolBlocks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The strip pool instances only the blocks its live flames need: none before the first flame, one block for a few
// flames, a second the frame a 33rd flame appears, and the trailing block removed only after the need has stayed
// lower for RIBBON_BLOCK_SHRINK_FRAMES frames in a row (hidden frames count as a need of none)
bool FSOLThrusterStorePoolBlocksTest::RunTest(const FString& /*parameters*/)
{
    FSOLThrusterStoreWorld world;
    if (!TestNotNull(TEXT("ribbons spawned"), world.Ribbons)
        || !TestTrue(TEXT("pool present"), world.PoolBlocks() >= 0))
    {
        return false;
    }
    world.SetNiagaraPx(THRSTORE_ALL_POOL_PX);
    constexpr int32 perBlock = SOL::Thruster::RIBBON_TRAILS_PER_BLOCK;
    constexpr int32 shrinkFrames = SOL::Thruster::RIBBON_BLOCK_SHRINK_FRAMES;
    TestEqual(TEXT("before any flame: no block"), world.PoolBlocks(), 0);

    // Returns count burning feeds, keys 100..
    const auto feeds = [](const int32 count)
    {
        TArray<FSOLRibbonFeed> out;
        for (int32 k = 0; k < count; ++k)
        {
            out.Add(ThrStoreFeed(100 + k, THRSTORE_NEAR_M + k, 1.0f));
        }
        return out;
    };
    double nowS = 1.0;
    const auto step = [&world, &nowS](const TArray<FSOLRibbonFeed>& frameFeeds)
    {
        nowS += 1.0 / 120.0;
        world.Frame(nowS, frameFeeds);
    };

    step(feeds(3));
    TestEqual(TEXT("3 flames: 1 block"), world.PoolBlocks(), 1);
    step(feeds(perBlock + 1));
    TestEqual(TEXT("33 flames: drawn"), world.Ribbons->GetPoolTrailCount(), perBlock + 1);
    TestEqual(TEXT("33 flames: 2 blocks at once"), world.PoolBlocks(), 2);

    // Back to 3: the second block stays for shrinkFrames - 1 frames, goes on the shrinkFrames-th
    for (int32 frame = 1; frame < shrinkFrames; ++frame)
    {
        step(feeds(3));
    }
    TestEqual(TEXT("need 1 for shrinkFrames - 1 frames: still 2 blocks"), world.PoolBlocks(), 2);
    step(feeds(3));
    TestEqual(TEXT("need 1 for shrinkFrames frames: 1 block"), world.PoolBlocks(), 1);

    // A run broken by a frame at the instanced count starts over
    step(feeds(perBlock + 1));
    for (int32 frame = 1; frame < shrinkFrames; ++frame)
    {
        step(frame == shrinkFrames / 2 ? feeds(perBlock + 1) : feeds(3));
    }
    step(feeds(3));
    TestEqual(TEXT("run broken half way: still 2 blocks"), world.PoolBlocks(), 2);

    // Hidden (no flame) long enough: no block (the low run under way ends at its largest need, 1 block, then a run of
    // hidden frames takes the last one)
    for (int32 frame = 0; frame < 2 * shrinkFrames + 2; ++frame)
    {
        step(feeds(0));
    }
    TestEqual(TEXT("no flame for long: no block"), world.PoolBlocks(), 0);
    TestEqual(TEXT("no flame: nothing drawn"), world.Ribbons->GetPoolTrailCount(), 0);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
