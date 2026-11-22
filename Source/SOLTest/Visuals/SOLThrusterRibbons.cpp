/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Visuals/SOLThrusterRibbons.h"

#include "Menu/SOLSettingsSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Visuals/SOLThrusterRibbonComponent.h"

#include "Async/ParallelFor.h"
#include "Camera/PlayerCameraManager.h"
#include "ConvexVolume.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "SceneView.h"
#include "Stats/Stats.h"
#include "UnrealClient.h"

#include <limits>

// Game-thread cost of the ribbons' per-frame render (store, split, Niagara pushes, strip fill; SDD 51 R9)
DECLARE_STATS_GROUP(TEXT("SOLThruster"), STATGROUP_SOLThruster, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("SOL_ThrusterRibbons"), STAT_SOL_ThrusterRibbons, STATGROUP_SOLThruster);

namespace
{
    // Developer A/B switch (SDD 51 R9)
    TAutoConsoleVariable<int32> CVarSOLThrusterRibbons(SOL::THRUSTER_RIBBONS_CVAR, 1,
        TEXT("1: thruster ribbons drawn (Niagara set and GPU strip pool); 0: none, and their work skipped (A/B "
            "profiling, SDD 51 R9)"));

    // Niagara threshold override (SDD 51 R7 verification)
    TAutoConsoleVariable<float> CVarSOLThrusterRibbonNiagaraPx(SOL::THRUSTER_RIBBON_NIAGARA_PX_CVAR, -1.0f,
        TEXT("On-screen length (reference px) from which an NPC ribbon is drawn by Niagara; below 0 the quality "
            "preset's RibbonNiagaraMinPx, a huge value sends every NPC ribbon to the strip pool (SDD 51 R7)"));

    // Store and scratch reservations (they only grow past these)
    constexpr int32 RIBBON_STORE_RESERVE = 2048;
    constexpr int32 RIBBON_POOL_INITIAL_TRAILS = 256;
    constexpr int32 RIBBON_PLAYER_TRAILS = 2;
    constexpr int32 RIBBON_POINTS = SOL::Thruster::FLAME_DRAWN_POINTS;  // Drawn points: the spine and its two caps
    constexpr int32 RIBBON_SPINE_POINTS = SOL::Thruster::FLAME_POINTS;
    constexpr int32 RIBBON_STORE_BATCH = 128;           // Live trails per task of the store pass

    // What the store pass decided for one live trail this frame
    enum class ERibbonClass : uint8
    {
        Kept,                                           // Kept, not drawn (not fed, or under the pixel rule)
        Release,                                        // Released
        Player,                                         // A player nozzle's trail (always drawn, R5)
        Shown,                                          // A shown NPC trail inside the view frustum
        Culled                                          // A shown NPC trail outside the view frustum
    };
    static_assert(SOL::Thruster::RIBBON_NIAGARA_POOL_TRAILS >= RIBBON_PLAYER_TRAILS,
        "the Niagara set holds the player's trails");

    //////////////////////////////////////////////////////////////////////////
    // Returns the Niagara user parameter names (hashed once)
    const FName& RibbonPositionsName()
    {
        static const FName name(SOL::ThrusterTrailParams::POSITIONS_CM);
        return name;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the Niagara colour array name
    const FName& RibbonColoursName()
    {
        static const FName name(SOL::ThrusterTrailParams::COLOURS);
        return name;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the Niagara width array name
    const FName& RibbonWidthsName()
    {
        static const FName name(SOL::ThrusterTrailParams::WIDTHS_CM);
        return name;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the Niagara flame header name
    const FName& RibbonFlameHeaderName()
    {
        static const FName name(SOL::ThrusterTrailParams::FLAME_HEADER);
        return name;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns how far a flame reaches from its ship's centre at most (nozzle offset, flame length, tail bend margin)
    double FlameReachM(const FSOLPlumeSample& head, const double plumeLengthM)
    {
        return static_cast<double>(head.OffsetM.Size())
            + SOL::Thruster::FLAME_REACH_BEND_MARGIN * SOLThruster::FlameLengthM(plumeLengthM);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the Niagara live point count name
    const FName& RibbonPointCountName()
    {
        static const FName name(SOL::ThrusterTrailParams::POINT_COUNT);
        return name;
    }
}

//////////////////////////////////////////////////////////////////////////
// Ticks once per frame in TG_PostUpdateWork; creates the root and the strip pool component
ASOLThrusterRibbons::ASOLThrusterRibbons()
{
    // After the universe update (a tickable subsystem) and the camera, before Niagara's TG_LastDemotable tick
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;
    Pool = CreateDefaultSubobject<USOLThrusterRibbonComponent>(TEXT("RibbonPool"));
    Pool->SetupAttachment(SceneRoot);
    Pool->SetUsingAbsoluteLocation(true);
    Pool->SetUsingAbsoluteRotation(true);
    Pool->SetUsingAbsoluteScale(true);
}

//////////////////////////////////////////////////////////////////////////
// Loads the system, creates the Niagara set, initialises the pool and reserves the store
void ASOLThrusterRibbons::BeginPlay()
{
    Super::BeginPlay();
    Settings = USOLSettingsSubsystem::Get(this);
    mTrails.Reserve(RIBBON_STORE_RESERVE);
    mHistories.Reserve(RIBBON_STORE_RESERVE);
    mLiveSlots.Reserve(RIBBON_STORE_RESERVE);
    mLiveClass.Reserve(RIBBON_STORE_RESERVE);
    mFreeTrails.Reserve(RIBBON_STORE_RESERVE);
    mTrailByKey.Reserve(RIBBON_STORE_RESERVE);
    mNpcFeeds.Reserve(RIBBON_STORE_RESERVE);
    mShown.Reserve(RIBBON_STORE_RESERVE);
    mNiagaraSlots.Reserve(SOL::Thruster::RIBBON_NIAGARA_POOL_TRAILS);
    mPoolSlots.Reserve(RIBBON_STORE_RESERVE);
    mPlayerSlots.Reserve(RIBBON_PLAYER_TRAILS);
    mNiagaraPositionsCm.SetNumZeroed(RIBBON_POINTS);
    mNiagaraColours.SetNumZeroed(RIBBON_POINTS);
    mNiagaraWidthsCm.SetNumZeroed(RIBBON_POINTS);
    Pool->SetWorldTransform(FTransform::Identity);
    if (!Pool->InitializePool(RIBBON_POOL_INITIAL_TRAILS))
    {
        UE_LOG(LogSOL, Warning, TEXT("ThrusterRibbons: %s missing; the strip pool never draws"),
            SOL::Paths::THRUSTER_RIBBON_MATERIAL);
    }

    // The Niagara set: one system instance per trail, created once with zero points and left inactive until a trail
    // is given to it; ticked last so it simulates the points pushed this frame (the same frame as the strip pool)
    UNiagaraSystem* system = LoadObject<UNiagaraSystem>(nullptr, SOL::Paths::THRUSTER_TRAILS_NIAGARA_SYSTEM);
    if (system == nullptr)
    {
        UE_LOG(LogSOL, Warning, TEXT("ThrusterRibbons: %s missing; the Niagara set never draws"),
            SOL::Paths::THRUSTER_TRAILS_NIAGARA_SYSTEM);
        return;
    }
    NiagaraTrails.Reserve(SOL::Thruster::RIBBON_NIAGARA_POOL_TRAILS);
    mNiagaraPointCounts.Init(0, SOL::Thruster::RIBBON_NIAGARA_POOL_TRAILS);
    mNiagaraHeaders.Init(FVector4f(0.0f, 0.0f, 0.0f, 0.0f), SOL::Thruster::RIBBON_NIAGARA_POOL_TRAILS);
    mNiagaraUsedS.Init(0.0, SOL::Thruster::RIBBON_NIAGARA_POOL_TRAILS);
    mNiagaraActive.Init(false, SOL::Thruster::RIBBON_NIAGARA_POOL_TRAILS);
    for (int32 index = 0; index < SOL::Thruster::RIBBON_NIAGARA_POOL_TRAILS; ++index)
    {
        UNiagaraComponent* component = NewObject<UNiagaraComponent>(this,
            FName(*FString::Printf(TEXT("RibbonTrail%d"), index)));
        component->bAutoActivate = false;
        component->SetupAttachment(SceneRoot);
        component->SetUsingAbsoluteLocation(true);
        component->SetUsingAbsoluteRotation(true);
        component->SetUsingAbsoluteScale(true);
        component->SetCastShadow(false);
        component->SetAsset(system);
        component->SetTickBehavior(ENiagaraTickBehavior::ForceTickLast);
        component->RegisterComponent();
        component->SetWorldTransform(FTransform::Identity);
        UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(component, RibbonPositionsName(),
            TConstArrayView<FVector3f>(mNiagaraPositionsCm));
        UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayColor(component, RibbonColoursName(),
            mNiagaraColours);
        UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(component, RibbonWidthsName(),
            mNiagaraWidthsCm);
        component->SetVariableInt(RibbonPointCountName(), 0);
        component->SetVariableVec4(RibbonFlameHeaderName(), FVector4::Zero());

        // Activated once and parked paused and hidden: a system deactivated and activated again was observed to draw
        // nothing (cause not confirmed, likely its one-shot particle burst), so an idle component is paused instead
        component->Activate(true);
        component->SetPaused(true);
        component->SetVisibility(false);
        NiagaraTrails.Add(component);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the ribbons are on (sol.ThrusterRibbons)
bool ASOLThrusterRibbons::AreRibbonsEnabled()
{
    return CVarSOLThrusterRibbons.GetValueOnGameThread() != 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the vanish length V of the current quality preset (Epic without settings)
double ASOLThrusterRibbons::GetVanishPx() const
{
    return SOLThruster::RibbonVanishPx(Settings != nullptr ? Settings->GetSavedValues().QualityPreset : 3);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a sphere touches last frame's view frustum (true without one)
bool ASOLThrusterRibbons::IsOnScreen(const FVector3d& centreM, const double radiusM) const
{
    return SphereTouchesFrustum(mFrustum, mHasFrustum, centreM, radiusM);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a sphere touches a frustum (true without one)
bool ASOLThrusterRibbons::SphereTouchesFrustum(const FConvexVolume& frustum, const bool bHasFrustum,
    const FVector3d& centreM, const double radiusM)
{
    return !bHasFrustum || frustum.IntersectSphere(centreM * SOL::METERS_TO_CM,
        static_cast<float>(FMath::Max(radiusM, 0.0) * SOL::METERS_TO_CM));
}

//////////////////////////////////////////////////////////////////////////
// Returns slotCount NPC feed slots for this frame (only the slots FeedNpc lists as pending are read back)
TArrayView<FSOLRibbonFeed> ASOLThrusterRibbons::BeginNpcFeeds(const int32 slotCount)
{
    const int32 count = FMath::Max(slotCount, 0);
    if (mNpcFeeds.Num() < count)
    {
        mNpcFeeds.SetNum(count);
        mPendingFeeds.SetNumUninitialized(count);
    }
    mPendingFeedCount.store(0, std::memory_order_relaxed);
    mNpcFeedCount = count;
    mFeedNowS = NowS();
    return TArrayView<FSOLRibbonFeed>(mNpcFeeds.GetData(), count);
}

//////////////////////////////////////////////////////////////////////////
// Feeds one NPC from a parallel task: in place when it has a trail, else pending when burning
void ASOLThrusterRibbons::FeedNpc(const int32 slot, const FSOLRibbonFeed& feed, FSOLRibbonFeed& outPending)
{
    outPending.bValid = false;
    if (slot != INDEX_NONE)
    {
        CommitFeedToSlot(slot, feed, mFeedNowS);
    }
    else if (feed.PlumeLengthM > 0.0 && feed.Pose.Brightness > 0.0f)
    {
        // Listed for CommitNpcFeeds by its slot (an atomic append: the feeders run in parallel)
        const int64 feedSlot = &outPending - mNpcFeeds.GetData();
        if (feedSlot >= 0 && feedSlot < mNpcFeedCount)
        {
            outPending = feed;
            outPending.bValid = true;
            mPendingFeeds[mPendingFeedCount.fetch_add(1, std::memory_order_relaxed)] = static_cast<int32>(feedSlot);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Commits the listed pending NPC feed slots into the store (creating their trails)
void ASOLThrusterRibbons::CommitNpcFeeds()
{
    if (!AreRibbonsEnabled())
    {
        mPendingFeedCount.store(0, std::memory_order_relaxed);
        mNpcFeedCount = 0;
        return;
    }
    const uint64 startCycles = FPlatformTime::Cycles64();
    const double nowS = mFeedNowS;

    // The listed slots in slot order, so the new trails take store slots in the same order every run
    TArrayView<int32> pending(mPendingFeeds.GetData(), FMath::Min(mPendingFeedCount.load(), mPendingFeeds.Num()));
    pending.Sort();
    for (const int32 slot : pending)
    {
        if (mNpcFeeds[slot].bValid && mNpcFeeds[slot].Key >= 0)
        {
            CommitFeed(mNpcFeeds[slot], nowS);
        }
    }
    mPendingFeedCount.store(0, std::memory_order_relaxed);
    mNpcFeedCount = 0;
    mLastPhaseMs[0] = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - startCycles);
}

//////////////////////////////////////////////////////////////////////////
// Commits one player nozzle's feed
void ASOLThrusterRibbons::FeedPlayer(const int32 nozzle, const FSOLRibbonFeed& feed)
{
    if (!AreRibbonsEnabled())
    {
        return;
    }
    FSOLRibbonFeed keyed = feed;
    keyed.Key = PlayerRibbonKey(nozzle);
    CommitFeed(keyed, NowS());
}

//////////////////////////////////////////////////////////////////////////
// Sets or clears the fixed verification clock
void ASOLThrusterRibbons::SetVerificationClock(const double nowS)
{
    mVerificationNowS = nowS;
}

//////////////////////////////////////////////////////////////////////////
// Sets or removes the verification feeder
void ASOLThrusterRibbons::SetVerificationFeeder(TFunction<void(const FVector3d& cameraM)> feeder)
{
    mVerificationFeeder = MoveTemp(feeder);
}

//////////////////////////////////////////////////////////////////////////
// Seeds an owner's history (verification)
void ASOLThrusterRibbons::SeedVerificationHistory(const int32 key, const FSOLPlumeHistory& history)
{
    const int32 slot = AcquireTrail(key);
    if (slot != INDEX_NONE)
    {
        mHistories[slot] = history;
        mTrails[slot].SampleCount = history.Num();
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the verification clock or the world's game time (R8: stops with the pause, ignores the time warp)
double ASOLThrusterRibbons::NowS() const
{
    return mVerificationNowS >= 0.0 ? mVerificationNowS : GetWorld()->GetTimeSeconds();
}

//////////////////////////////////////////////////////////////////////////
// Finds or creates the store slot of a key
int32 ASOLThrusterRibbons::AcquireTrail(const int32 key)
{
    if (const int32* found = mTrailByKey.Find(key))
    {
        return *found;
    }
    int32 slot = INDEX_NONE;
    if (mFreeTrails.Num() > 0)
    {
        slot = mFreeTrails.Pop(EAllowShrinking::No);
    }
    else
    {
        slot = mTrails.AddDefaulted();
        mHistories.AddDefaulted();
    }
    mHistories[slot].Reset();
    FSOLRibbonTrail& trail = mTrails[slot];
    trail.Key = key;
    trail.FedFrame = 0;
    trail.PixelFade = 0.0;
    trail.SampleCount = 0;
    trail.LastBurningBrightness = 0.0f;
    trail.NewestBurningS = 0.0;
    trail.bCutoff = false;
    trail.bLive = true;
    trail.LiveIndex = mLiveSlots.Add(slot);
    mTrailByKey.Add(key, slot);
    return slot;
}

//////////////////////////////////////////////////////////////////////////
// Frees a store slot (swap-removed from the live list)
void ASOLThrusterRibbons::ReleaseTrail(const int32 slot)
{
    FSOLRibbonTrail& trail = mTrails[slot];
    mTrailByKey.Remove(trail.Key);
    const int32 liveIndex = trail.LiveIndex;
    mLiveSlots.RemoveAtSwap(liveIndex, EAllowShrinking::No);
    if (liveIndex < mLiveSlots.Num())
    {
        mTrails[mLiveSlots[liveIndex]].LiveIndex = liveIndex;
    }
    trail.LiveIndex = INDEX_NONE;
    trail.bLive = false;
    trail.Key = INDEX_NONE;
    mFreeTrails.Add(slot);
}

//////////////////////////////////////////////////////////////////////////
// Copies a feed into its trail and pushes its pose when due; a ship that does not burn and has no trail gets none
void ASOLThrusterRibbons::CommitFeed(const FSOLRibbonFeed& feed, const double nowS)
{
    const bool bBurning = feed.PlumeLengthM > 0.0 && feed.Pose.Brightness > 0.0f;
    int32 slot = INDEX_NONE;
    if (const int32* found = mTrailByKey.Find(feed.Key))
    {
        slot = *found;
    }
    else if (bBurning)
    {
        slot = AcquireTrail(feed.Key);
    }
    if (slot != INDEX_NONE)
    {
        CommitFeedToSlot(slot, feed, nowS);
    }
}

//////////////////////////////////////////////////////////////////////////
// Copies a feed into an existing trail, runs the spool-down (R10-Q4: b falls to 0 over FLAME_SHUTDOWN_S with L_p and c
// held) and pushes the pose when due, dark poses included so the flame's gas keeps its emission brightness; touches
// that trail only, so distinct slots may be committed from parallel tasks
void ASOLThrusterRibbons::CommitFeedToSlot(const int32 slot, const FSOLRibbonFeed& feed, const double nowS)
{
    const bool bBurning = feed.PlumeLengthM > 0.0 && feed.Pose.Brightness > 0.0f;
    FSOLRibbonTrail& trail = mTrails[slot];
    trail.ShipPositionM = feed.ShipPositionM;
    trail.DistanceM = feed.DistanceM;
    trail.NozzleRadiusM = feed.NozzleRadiusM;
    trail.FedFrame = GFrameCounter;
    double brightness = 0.0;
    if (bBurning)
    {
        trail.PlumeLengthM = feed.PlumeLengthM;
        trail.ColourScalar = feed.ColourScalar;
        trail.LastBurningBrightness = feed.Pose.Brightness;
        trail.bCutoff = false;
        brightness = static_cast<double>(feed.Pose.Brightness);
    }
    else
    {
        if (!trail.bCutoff)
        {
            trail.bCutoff = true;
            trail.CutoffS = nowS;
        }
        brightness = SOLThruster::FlameShutdownBrightness(trail.LastBurningBrightness, nowS - trail.CutoffS);
    }
    trail.Head = feed.Pose;
    trail.Head.EmitTimeS = nowS;
    trail.Head.Brightness = static_cast<float>(brightness);
    if (brightness > 0.0)
    {
        trail.NewestBurningS = nowS;
    }
    FSOLPlumeHistory& history = mHistories[slot];
    if (history.ShouldSample(nowS))
    {
        history.Push(trail.Head);
        trail.SampleCount = history.Num();
    }
}

//////////////////////////////////////////////////////////////////////////
// Hides both renderers (once) and drops the feeds and histories
void ASOLThrusterRibbons::HideAll()
{
    mPendingFeedCount.store(0, std::memory_order_relaxed);
    mNpcFeedCount = 0;
    mNiagaraTrailsDrawn = 0;
    mPoolTrailsDrawn = 0;
    if (mIsHidden)
    {
        return;
    }
    mIsHidden = true;
    for (int32 index = 0; index < NiagaraTrails.Num(); ++index)
    {
        PushNiagaraTrail(index, INDEX_NONE, 0.0, FVector3d::ZeroVector, 0.0);
        ParkNiagaraComponent(index);
    }
    Pool->DrawTrails(0, FVector3d::ZeroVector, [](int32, TArrayView<FVector4f>) {});
    while (mLiveSlots.Num() > 0)
    {
        ReleaseTrail(mLiveSlots.Last());
    }
    mLiveTrails = 0;
}

//////////////////////////////////////////////////////////////////////////
// Appends the NPC ribbons drawn last frame by each renderer (verification)
void ASOLThrusterRibbons::GetDrawnNpcTrails(TArray<FSOLRibbonDrawnTrail>& outTrails) const
{
    // Returns one store slot's entry
    const auto entry = [this](const int32 slot, const bool bNiagara)
    {
        const FSOLRibbonTrail& trail = mTrails[slot];
        return FSOLRibbonDrawnTrail{ trail.ShipPositionM, trail.RibbonPx, trail.DistanceM, trail.Key, bNiagara };
    };
    // mNiagaraSlots holds exactly the NPC slots Niagara drew (a resume frame's slot is in mPoolSlots); the pool may also
    // draw a player trail (a resume frame or no Niagara system), which is not an NPC entry
    for (int32 index = 0; index < mNiagaraNpcDrawn && index < mNiagaraSlots.Num(); ++index)
    {
        outTrails.Add(entry(mNiagaraSlots[index], true));
    }
    for (int32 index = 0; index < mPoolTrailsDrawn && index < mPoolSlots.Num(); ++index)
    {
        if (mTrails[mPoolSlots[index]].Key >= 0)
        {
            outTrails.Add(entry(mPoolSlots[index], false));
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the active Niagara set components (verification)
int32 ASOLThrusterRibbons::GetActiveNiagaraCount() const
{
    int32 active = 0;
    for (const bool bActive : mNiagaraActive)
    {
        active += bActive ? 1 : 0;
    }
    return active;
}

//////////////////////////////////////////////////////////////////////////
// Marks a component used and resumes it when parked; false on its resume frame
bool ASOLThrusterRibbons::UseNiagaraComponent(const int32 component, const double nowS)
{
    mNiagaraUsedS[component] = nowS;
    if (mNiagaraActive[component])
    {
        return true;
    }
    NiagaraTrails[component]->SetPaused(false);
    NiagaraTrails[component]->SetVisibility(true);
    mNiagaraActive[component] = true;
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Parks a Niagara set component: paused (no simulation) and hidden
void ASOLThrusterRibbons::ParkNiagaraComponent(const int32 component)
{
    if (mNiagaraActive.IsValidIndex(component) && mNiagaraActive[component])
    {
        NiagaraTrails[component]->SetPaused(true);
        NiagaraTrails[component]->SetVisibility(false);
        mNiagaraActive[component] = false;
    }
}

//////////////////////////////////////////////////////////////////////////
// Writes one Niagara component's arrays and flame header from a trail (always FLAME_DRAWN_POINTS entries: the spine and
// its two end caps, padded with the last point at alpha and width 0, so Niagara's 1 cm rule drops the padding); zero
// points for none
void ASOLThrusterRibbons::PushNiagaraTrail(const int32 component, const int32 slot, const double nowS,
    const FVector3d& cameraCm, const double deviceFocalPx)
{
    UNiagaraComponent* niagara = NiagaraTrails[component];
    const FSOLRibbonTrail* trail = slot != INDEX_NONE ? &mTrails[slot] : nullptr;
    if (trail == nullptr)
    {
        if (mNiagaraPointCounts[component] != 0)
        {
            niagara->SetVariableInt(RibbonPointCountName(), 0);
            mNiagaraPointCounts[component] = 0;
        }
        return;
    }
    const uint64 buildCycles = FPlatformTime::Cycles64();
    FSOLRibbonPoint spine[RIBBON_SPINE_POINTS];
    FSOLRibbonPoint points[RIBBON_POINTS];
    const FVector3d anchorM = mLastAnchorCm / 100.0;
    const int32 spineCount = SOLThruster::BuildFlamePoints(mHistories[slot], trail->Head, trail->ShipPositionM,
        trail->PlumeLengthM, trail->NozzleRadiusM, SOL::Thruster::FLAME_EXHAUST_SPEED_MPS, trail->PixelFade, anchorM,
        MakeArrayView(spine));
    const int32 count = SOLThruster::AddFlameCaps(TConstArrayView<FSOLRibbonPoint>(spine, spineCount),
        trail->NozzleRadiusM, MakeArrayView(points));
    for (int32 p = 0; p < RIBBON_POINTS; ++p)
    {
        const FSOLRibbonPoint& point = points[FMath::Clamp(p, 0, FMath::Max(count - 1, 0))];
        const FVector3f positionCm = point.PositionM * 100.0f;
        mNiagaraPositionsCm[p] = positionCm;

        // Colour = (s / L_f, alpha, sharpness, cap fraction): the flame pixel's arc coordinate (linear along each
        // straight segment, so exact after interpolation; a cap shares its end point's), the point's alpha, the
        // thin-flame shading weight and 0 on a cap point (1 elsewhere), the same values the strip pool derives from its
        // texels (SOLTH_FlameSharpness; a cap point takes its end point's width for the weight and is drawn at width 0)
        const float arcFraction = static_cast<float>(FMath::Clamp(p - 1, 0, RIBBON_SPINE_POINTS - 1))
            / static_cast<float>(RIBBON_SPINE_POINTS - 1);
        if (p < count)
        {
            // The 1 px device floor at the point's distance, exactly as SOLTH_FlameVertex applies it
            const bool bCap = p == 0 || p == count - 1;
            const FSOLRibbonPoint& shade = points[FMath::Clamp(p, 1, FMath::Max(count - 2, 1))];
            const double distanceM = (mLastAnchorCm + FVector3d(positionCm) - cameraCm).Size() / 100.0;
            const double drawnM = SOLThruster::RibbonDrawnWidthM(shade.WidthM, distanceM, deviceFocalPx,
                SOL::Thruster::RIBBON_MIN_WIDTH_PX);
            mNiagaraWidthsCm[p] = bCap ? 0.0f : static_cast<float>(drawnM * 100.0);
            mNiagaraColours[p] = FLinearColor(arcFraction, point.Alpha,
                static_cast<float>(SOLThruster::FlameSharpness(shade.WidthM, drawnM, distanceM, deviceFocalPx)),
                bCap ? 0.0f : 1.0f);
        }
        else
        {
            mNiagaraWidthsCm[p] = 0.0f;
            mNiagaraColours[p] = FLinearColor(arcFraction, 0.0f, 0.0f, 0.0f);
        }
    }
    mLastPhaseMs[9] += FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - buildCycles);

    // The header only when it changes (a Niagara user parameter set is a deferred, editor-tracked write)
    const FVector4f header(trail->ColourScalar, trail->Head.Brightness, static_cast<float>(trail->PlumeLengthM),
        trail->NozzleRadiusM);
    if (header != mNiagaraHeaders[component])
    {
        niagara->SetVariableVec4(RibbonFlameHeaderName(), FVector4(header));
        mNiagaraHeaders[component] = header;
    }
    const uint64 moveCycles = FPlatformTime::Cycles64();
    if (!niagara->GetComponentLocation().Equals(mLastAnchorCm, 0.0))
    {
        niagara->SetWorldLocation(mLastAnchorCm);
    }
    const uint64 arrayCycles = FPlatformTime::Cycles64();
    SetNiagaraArrays(component);
    if (count != mNiagaraPointCounts[component])
    {
        niagara->SetVariableInt(RibbonPointCountName(), count);
        mNiagaraPointCounts[component] = count;
    }
    const uint64 endCycles = FPlatformTime::Cycles64();
    mLastPhaseMs[5] += FPlatformTime::ToMilliseconds64(endCycles - arrayCycles);
    mLastPhaseMs[6] += FPlatformTime::ToMilliseconds64(arrayCycles - moveCycles);
}

//////////////////////////////////////////////////////////////////////////
// Hands one flame's three arrays to a Niagara set component
void ASOLThrusterRibbons::SetNiagaraArrays(const int32 component)
{
    UNiagaraComponent* niagara = NiagaraTrails[component];
    UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(niagara, RibbonPositionsName(),
        TConstArrayView<FVector3f>(mNiagaraPositionsCm));
    UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayColor(niagara, RibbonColoursName(), mNiagaraColours);
    UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(niagara, RibbonWidthsName(), mNiagaraWidthsCm);
}

//////////////////////////////////////////////////////////////////////////
// Updates the cached projection when its inputs change, then the focal, aspect and frustum from the camera now
bool ASOLThrusterRibbons::UpdateView(const ULocalPlayer& localPlayer, const APlayerCameraManager& cameraManager,
    const FVector3d& cameraCm)
{
    FViewport* viewport = localPlayer.ViewportClient != nullptr ? localPlayer.ViewportClient->Viewport : nullptr;
    if (viewport == nullptr)
    {
        mHasProjection = false;
        mHasFrustum = false;
        return false;
    }

    // The projection (GetProjectionData is costly: view extensions gathered, editor aspect broadcasts) depends only on
    // the viewport size, the field of view and the aspect settings, so it is rebuilt only when one changes
    const FMinimalViewInfo& pov = cameraManager.GetCameraCacheView();
    const FIntPoint viewportSize = viewport->GetSizeXY();
    const float fovDeg = cameraManager.GetFOVAngle();
    if (!mHasProjection || viewportSize != mProjectionViewportSize || fovDeg != mProjectionFovDeg
        || pov.AspectRatio != mProjectionAspect || pov.bConstrainAspectRatio != mProjectionConstrained)
    {
        FSceneViewProjectionData projection;
        mHasProjection = localPlayer.GetProjectionData(viewport, projection);
        if (!mHasProjection)
        {
            mHasFrustum = false;
            return false;
        }
        mProjectionMatrix = projection.ProjectionMatrix;
        mProjectionRect = projection.GetConstrainedViewRect();
        mProjectionViewportSize = viewportSize;
        mProjectionFovDeg = fovDeg;
        mProjectionAspect = pov.AspectRatio;
        mProjectionConstrained = pov.bConstrainAspectRatio;
        mViewAspect = mProjectionRect.Height() > 0
            ? static_cast<double>(mProjectionRect.Width()) / mProjectionRect.Height()
            : SOL::Thruster::PLAYER_FALLBACK_ASPECT;
    }

    // The view frustum (side planes, render-space cm) from the camera now, as
    // FSceneViewProjectionData::ComputeViewProjectionMatrix builds it: an NPC flame wholly outside it is neither built
    // nor drawn, and next frame's NPC feeders skip ships outside it (IsOnScreen, R10-Q8)
    const FMatrix viewRotation = FInverseRotationMatrix(cameraManager.GetCameraRotation()) * FMatrix(
        FPlane(0.0, 0.0, 1.0, 0.0), FPlane(1.0, 0.0, 0.0, 0.0), FPlane(0.0, 1.0, 0.0, 0.0), FPlane(0.0, 0.0, 0.0, 1.0));
    GetViewFrustumBounds(mFrustum, FTranslationMatrix(-cameraCm) * viewRotation * mProjectionMatrix, false);
    mHasFrustum = mFrustum.Planes.Num() > 0;
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Renders this frame's ribbons: release, pixel rule, split, the Niagara set and the strip pool
void ASOLThrusterRibbons::Tick(const float deltaSeconds)
{
    Super::Tick(deltaSeconds);
    SCOPE_CYCLE_COUNTER(STAT_SOL_ThrusterRibbons);
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLThrusterRibbons::Tick);
    const uint64 startCycles = FPlatformTime::Cycles64();
    ON_SCOPE_EXIT
    {
        mLastTickMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - startCycles);
    };
    mNiagaraNpcDrawn = 0;
    mNiagaraPlayerDrawn = 0;
    mLastPhaseMs[5] = 0.0;
    mLastPhaseMs[6] = 0.0;
    mLastPhaseMs[9] = 0.0;
    if (!AreRibbonsEnabled())
    {
        HideAll();
        return;
    }
    mIsHidden = false;

    // The camera and the real projection (the device focal of the 1 px floor, R1)
    const APlayerController* controller = GetWorld()->GetFirstPlayerController();
    const APlayerCameraManager* cameraManager = controller != nullptr ? controller->PlayerCameraManager : nullptr;
    if (cameraManager == nullptr)
    {
        return;
    }
    const FVector3d cameraCm = cameraManager->GetCameraLocation();
    if (mVerificationFeeder)
    {
        mVerificationFeeder(cameraCm / 100.0);
    }
    double deviceFocalPx = 0.0;
    double aspect = SOL::Thruster::PLAYER_FALLBACK_ASPECT;
    const ULocalPlayer* localPlayer = controller->GetLocalPlayer();
    if (localPlayer != nullptr && UpdateView(*localPlayer, *cameraManager, cameraCm))
    {
        deviceFocalPx = SOLThruster::RibbonDeviceFocalPx(mProjectionMatrix.M[1][1], mProjectionRect.Height());
        aspect = mViewAspect;
    }
    const double focalRefPx = SOLThruster::FocalRefPx(cameraManager->GetFOVAngle(), aspect);
    const int32 quality = Settings != nullptr ? Settings->GetSavedValues().QualityPreset : 3;
    const double vanishPx = GetVanishPx();
    const float overridePx = CVarSOLThrusterRibbonNiagaraPx.GetValueOnGameThread();
    const double niagaraMinPx = overridePx >= 0.0f ? static_cast<double>(overridePx)
        : SOLThruster::RibbonNiagaraMinPx(quality);
    const double nowS = NowS();
    mLastAnchorCm = SOLThruster::RibbonAnchorHoldCm(mLastAnchorCm, mHasAnchor, cameraCm);
    mHasAnchor = true;
    mLastDeviceFocalPx = deviceFocalPx;
    const FVector3d anchorM = mLastAnchorCm / 100.0;

    // Release flames whose gas has left; the pixel rule for the fed NPC flames (the player is never gated, R5); a
    // shown NPC flame whose whole extent (nozzle offset, flame length with a bend margin, the nozzle width and a 2 px
    // floor margin) lies outside the view frustum is not offered to the split, so it is neither built nor drawn
    uint64 phaseCycles = FPlatformTime::Cycles64();
    mLastPhaseMs[7] = FPlatformTime::ToMilliseconds64(phaseCycles - startCycles);
    // Records the time since the previous phase ended as this phase's verification time, and starts the next phase
    const auto endPhase = [this, &phaseCycles](const int32 phase)
    {
        const uint64 now = FPlatformTime::Cycles64();
        mLastPhaseMs[phase] = FPlatformTime::ToMilliseconds64(now - phaseCycles);
        phaseCycles = now;
    };
    const double floorMarginPerM = deviceFocalPx > 0.0 ? 2.0 / deviceFocalPx : 0.0;
    mShown.Reset();
    mPlayerSlots.Reset();
    mLiveTrails = 0;
    mCulledTrails = 0;
    // Parallel over the dense live list: each task updates its own trails' pixel rule and classifies them (the
    // trails were just written by the parallel feeders, so a serial pass would pay their cache misses one by one)
    const int32 liveCount = mLiveSlots.Num();
    mLiveClass.SetNumUninitialized(liveCount, EAllowShrinking::No);
    const uint64 frame = GFrameCounter;
    ParallelFor(TEXT("SOLThrusterRibbonStore"), liveCount, RIBBON_STORE_BATCH, [&](const int32 live)
    {
        FSOLRibbonTrail& trail = mTrails[mLiveSlots[live]];
        const bool bFed = trail.FedFrame == frame;
        const int32 samples = trail.SampleCount;
        const double newestBurningAgeS = samples > 0 ? nowS - trail.NewestBurningS
            : std::numeric_limits<double>::quiet_NaN();
        const double flameLifeS = SOLThruster::FlameLengthM(trail.PlumeLengthM) / SOL::Thruster::FLAME_EXHAUST_SPEED_MPS;
        const bool bPlayer = trail.Key < 0;
        if (bFed)
        {
            trail.RibbonPx = SOLThruster::RibbonPixelLength(trail.PlumeLengthM, trail.DistanceM, focalRefPx);
            trail.PixelFade = bPlayer ? 1.0 : SOLThruster::RibbonPixelFade(trail.RibbonPx, vanishPx);
        }
        ERibbonClass result = ERibbonClass::Kept;
        if (SOLThruster::RibbonTrailReleasable(bFed, samples, newestBurningAgeS, flameLifeS, trail.PixelFade))
        {
            result = ERibbonClass::Release;
        }
        else if (bFed && bPlayer)
        {
            result = ERibbonClass::Player;
        }
        else if (bFed && SOLThruster::RibbonShown(trail.RibbonPx, vanishPx, trail.Head.Brightness > 0.0f, samples > 0))
        {
            result = ERibbonClass::Shown;
            if (mHasFrustum)
            {
                const double reachM = FlameReachM(trail.Head, trail.PlumeLengthM);
                const double radiusM = reachM + 2.0 * trail.NozzleRadiusM
                    + (trail.DistanceM + reachM) * floorMarginPerM;
                if (!mFrustum.IntersectSphere(trail.ShipPositionM * SOL::METERS_TO_CM,
                    static_cast<float>(radiusM * SOL::METERS_TO_CM)))
                {
                    result = ERibbonClass::Culled;
                }
            }
        }
        mLiveClass[live] = static_cast<uint8>(result);
    });

    // Serial, backwards so a release (swap with the last live entry, already visited) never skips a trail
    for (int32 live = liveCount - 1; live >= 0; --live)
    {
        const int32 slot = mLiveSlots[live];
        switch (static_cast<ERibbonClass>(mLiveClass[live]))
        {
        case ERibbonClass::Release:
            ReleaseTrail(slot);
            continue;
        case ERibbonClass::Player:
            if (mPlayerSlots.Num() < RIBBON_PLAYER_TRAILS)
            {
                mPlayerSlots.Add(slot);
            }
            break;
        case ERibbonClass::Shown:
            mShown.Add(FSOLRibbonShown{ slot, mTrails[slot].RibbonPx });
            break;
        case ERibbonClass::Culled:
            ++mCulledTrails;
            break;
        default:
            break;
        }
        ++mLiveTrails;
    }
    endPhase(1);

    // The split by pixel size (R7): the player's two trails always take two Niagara components
    SOLThruster::SplitRibbonRenderers(mShown, niagaraMinPx,
        SOL::Thruster::RIBBON_NIAGARA_POOL_TRAILS - RIBBON_PLAYER_TRAILS, mNiagaraSlots, mPoolSlots);
    endPhase(2);
    int32 component = 0;
    if (NiagaraTrails.Num() > 0)
    {
        // Gives a trail the next component; on that component's activation frame the strip pool draws the trail
        const auto giveNiagara = [this, &component, nowS, &cameraCm, deviceFocalPx](const int32 slot)
        {
            const int32 index = component++;
            const bool bReady = UseNiagaraComponent(index, nowS);
            PushNiagaraTrail(index, bReady ? slot : INDEX_NONE, nowS, cameraCm, deviceFocalPx);
            if (!bReady)
            {
                mPoolSlots.Add(slot);
            }
            return bReady;
        };
        for (const int32 slot : mPlayerSlots)
        {
            mNiagaraPlayerDrawn += giveNiagara(slot) ? 1 : 0;
        }

        // The NPC slots Niagara draws, compacted in place (a resume frame's slot went to the pool, and so does one past
        // the last component), so mNiagaraSlots lists exactly what Niagara drew
        for (int32 index = 0; index < mNiagaraSlots.Num(); ++index)
        {
            const int32 slot = mNiagaraSlots[index];
            if (component >= NiagaraTrails.Num())
            {
                mPoolSlots.Add(slot);
            }
            else if (giveNiagara(slot))
            {
                mNiagaraSlots[mNiagaraNpcDrawn++] = slot;
            }
        }
        mNiagaraSlots.SetNum(mNiagaraNpcDrawn, EAllowShrinking::No);

        // The unused components: no points, and inactive once idle for RIBBON_NIAGARA_IDLE_S
        for (int32 index = component; index < NiagaraTrails.Num(); ++index)
        {
            PushNiagaraTrail(index, INDEX_NONE, nowS, cameraCm, deviceFocalPx);
            if (!(nowS - mNiagaraUsedS[index] < SOL::Thruster::RIBBON_NIAGARA_IDLE_S))
            {
                ParkNiagaraComponent(index);
            }
        }
    }
    else
    {
        // Without the Niagara system (it failed to load) the strip pool draws every flame, the player's included
        mPoolSlots.Append(mPlayerSlots);
        mPoolSlots.Append(mNiagaraSlots);
        mNiagaraSlots.Reset();
    }
    mNiagaraTrailsDrawn = mNiagaraPlayerDrawn + mNiagaraNpcDrawn;
    endPhase(3);

    // The strip pool: every other shown ribbon, the same point builder in a ParallelFor
    const TConstArrayView<FSOLRibbonTrail> trails(mTrails);
    const TConstArrayView<FSOLPlumeHistory> histories(mHistories);
    const TConstArrayView<int32> poolSlots(mPoolSlots);
    Pool->DrawTrails(mPoolSlots.Num(), mLastAnchorCm, [trails, histories, poolSlots, nowS, anchorM](
        const int32 trail, TArrayView<FVector4f> texels)
    {
        // The spine is built in place at drawn points 1.., so AddFlameCaps only writes the two caps
        const FSOLRibbonTrail& source = trails[poolSlots[trail]];
        FSOLRibbonPoint points[RIBBON_POINTS];
        const int32 spineCount = SOLThruster::BuildFlamePoints(histories[poolSlots[trail]], source.Head,
            source.ShipPositionM, source.PlumeLengthM, source.NozzleRadiusM, SOL::Thruster::FLAME_EXHAUST_SPEED_MPS,
            source.PixelFade, anchorM, MakeArrayView(points + 1, RIBBON_SPINE_POINTS));
        const int32 count = SOLThruster::AddFlameCaps(TConstArrayView<FSOLRibbonPoint>(points + 1, spineCount),
            source.NozzleRadiusM, MakeArrayView(points));
        SOLThruster::PackRibbonTexels(TConstArrayView<FSOLRibbonPoint>(points, count), source.ColourScalar,
            source.Head.Brightness, static_cast<float>(source.PlumeLengthM), source.NozzleRadiusM, texels);
    });
    mPoolTrailsDrawn = mPoolSlots.Num();
    mLastPhaseMs[8] = Pool->GetLastFillMs();
    endPhase(4);
}
