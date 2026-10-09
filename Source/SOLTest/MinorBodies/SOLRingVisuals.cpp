/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "MinorBodies/SOLRingVisuals.h"

#include "MinorBodies/SOLMinorBodyFragments.h"
#include "MinorBodies/SOLMinorBodyVisualsUtil.h"
#include "MinorBodies/SOLRingDense.h"
#include "MinorBodies/SOLRingPatch.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLRenderPlacement.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutionContext.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns a ring's real interior gap bands (resonance + shepherd-moon), mirroring USOLRingSubsystem::
    // BuildRingState's Saturn special case (Pan/Daphnis are the only registered moons orbiting within any ring -
    // SDD 6 Amendment 4). Deliberately duplicated rather than exposed from USOLRingSubsystem (whose FSOLRingState
    // is private and not meant as a cross-subsystem accessor; both this actor and that subsystem instead call the
    // same pure SOLPlanetRing functions independently, same convention already used between USOLMinorBodySubsystem
    // and USOLRingSubsystem). Ascending by LowM (SOLPlanetRing::AllGapBandsM's own guarantee).
    TArray<FSOLRadiusBandM> BuildRingGapBandsM(const FSOLBodyRegistry& registry, const FSOLPlanetRingDef& ringDef)
    {
        TArray<double> moonOrbitRadiiM;
        TArray<double> moonGapHalfWidthsM;
        if (ringDef.PlanetName == FName(SOL::BodyNames::SATURN))
        {
            const int32 panIndex = registry.FindByName(FName(SOL::BodyNames::PAN));
            const int32 daphnisIndex = registry.FindByName(FName(SOL::BodyNames::DAPHNIS));
            if (panIndex != INDEX_NONE)
            {
                moonOrbitRadiiM.Add(registry.GetElements(panIndex).A0AU * SOL::AU_M);
                moonGapHalfWidthsM.Add(SOLPlanetRing::SATURN_PAN_GAP_HALF_WIDTH_M);
            }
            if (daphnisIndex != INDEX_NONE)
            {
                moonOrbitRadiiM.Add(registry.GetElements(daphnisIndex).A0AU * SOL::AU_M);
                moonGapHalfWidthsM.Add(SOLPlanetRing::SATURN_DAPHNIS_GAP_HALF_WIDTH_M);
            }
        }
        return SOLPlanetRing::AllGapBandsM(ringDef, moonOrbitRadiiM, moonGapHalfWidthsM);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the far-field material's ring-profile index for a host planet (0 Jupiter, 1 Saturn, 2 Uranus,
    // 3 Neptune; SDD 6 Amendment 15), or -1 to let the asset infer it from the outer radius
    int32 ProfileIndexForPlanet(FName planetName)
    {
        if (planetName == FName(SOL::BodyNames::JUPITER)) { return SOL::RING_PROFILE_INDEX_JUPITER; }
        if (planetName == FName(SOL::BodyNames::SATURN)) { return SOL::RING_PROFILE_INDEX_SATURN; }
        if (planetName == FName(SOL::BodyNames::URANUS)) { return SOL::RING_PROFILE_INDEX_URANUS; }
        if (planetName == FName(SOL::BodyNames::NEPTUNE)) { return SOL::RING_PROFILE_INDEX_NEPTUNE; }
        return SOL::RING_PROFILE_INDEX_INFER;
    }
}

//////////////////////////////////////////////////////////////////////////
// Creates the root, the near-field ISM, the far-field Niagara component and the dense-layer Niagara component
ASOLRingVisuals::ASOLRingVisuals()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;

    // Pinned at the Unreal origin whatever the actor's transform, so instance transforms are world transforms -
    // same reasoning as ASOLAsteroidBeltVisuals
    NearFieldMesh = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("NearField"));
    NearFieldMesh->SetupAttachment(SceneRoot);
    SOLMinorBodyVisualsUtil::MakeVisualOnlyPrimitive(NearFieldMesh);
    NearFieldMesh->SetUsingAbsoluteLocation(true);
    NearFieldMesh->SetUsingAbsoluteRotation(true);
    NearFieldMesh->SetUsingAbsoluteScale(true);

    // Same absolute-transform pinning: its own world transform is set explicitly every frame (PlaceFarFieldNiagara)
    // from the same render-origin snapshot the near field uses, rather than relying on attachment math.
    // bAutoActivate=false + an explicit Activate(true) once all User Parameters are set in BeginPlay (below) makes
    // the one-time burst's first read of its own parameters deterministic, rather than relying on auto-activation
    // racing against SetVariable* in an unspecified order
    FarFieldNiagara = CreateDefaultSubobject<UNiagaraComponent>(TEXT("FarField"));
    FarFieldNiagara->SetupAttachment(SceneRoot);
    FarFieldNiagara->SetUsingAbsoluteLocation(true);
    FarFieldNiagara->SetUsingAbsoluteRotation(true);
    FarFieldNiagara->SetUsingAbsoluteScale(true);
    FarFieldNiagara->bAutoActivate = false;

    // Dense rock layer: same absolute-transform pinning and explicit one-time activation as the far field; its world
    // transform is set every frame while visible (UpdateDenseLayer)
    DenseNiagara = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Dense"));
    DenseNiagara->SetupAttachment(SceneRoot);
    DenseNiagara->SetUsingAbsoluteLocation(true);
    DenseNiagara->SetUsingAbsoluteRotation(true);
    DenseNiagara->SetUsingAbsoluteScale(true);
    DenseNiagara->bAutoActivate = false;

    // Forwards each chunk to ProcessChunk
    mChunkFunction = [this](FMassExecutionContext& context)
    {
        ProcessChunk(context);
    };
}

//////////////////////////////////////////////////////////////////////////
// Resolves the ring definition and planet index, wires the far-field Niagara system's User Parameters, builds the
// near-field Mass query and instances, and subscribes to universe updates
void ASOLRingVisuals::BeginPlay()
{
    Super::BeginPlay();

    UWorld* world = GetWorld();
    AnchorSubsystem = world->GetSubsystem<USOLAnchorSubsystem>();
    BodyRegistry = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    SimClock = world->GetSubsystem<USOLSimClockSubsystem>();
    UMassEntitySubsystem* massEntities = world->GetSubsystem<UMassEntitySubsystem>();
    if (AnchorSubsystem == nullptr || BodyRegistry == nullptr || massEntities == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("RingVisuals %s: Mass or universe subsystems missing; nothing will be drawn"),
            *GetName());
        return;
    }

    const TArray<FSOLPlanetRingDef> rings = SOLPlanetRing::RealRings();
    const FSOLPlanetRingDef* ringDef =
        rings.FindByPredicate([this](const FSOLPlanetRingDef& candidate) { return candidate.PlanetName == PlanetName; });
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    mPlanetIndex = registry.FindByName(PlanetName);
    if (ringDef == nullptr || mPlanetIndex == INDEX_NONE)
    {
        UE_LOG(LogSOL, Error, TEXT("RingVisuals %s: '%s' has no ring definition or is not in the registry"),
            *GetName(), *PlanetName.ToString());
        return;
    }
    mRingDef = *ringDef;
    mRingNormal = registry.GetOrientation(mPlanetIndex).RotateVector(FVector3d(0.0, 0.0, 1.0));
    // mRingNormal is ECLIPTIC axes (for DistanceOutsideRingVolumeM, which compares it against ecliptic-frame
    // positions); the far field's own rotation needs Unreal axes, so convert and cache the quaternion once here
    // rather than reconstructing it every frame via FindBetweenNormals (which also loses any roll about the normal)
    mFarFieldRotation = FQuat(SOLRender::EclipticToUnreal(registry.GetOrientation(mPlanetIndex)));
    mRingOrientation = registry.GetOrientation(mPlanetIndex);
    mSunIndex = registry.FindByName(FName(SOL::BodyNames::SUN));
    mPlanetGM = registry.GetGM(mPlanetIndex);
    mLastSecondsSinceJ2000 = SimClock != nullptr ? SimClock->GetClock().GetSecondsSinceJ2000() : 0.0;
    mRingAxisE1 = mRingOrientation.RotateVector(FVector3d(1.0, 0.0, 0.0));
    mRingAxisE2 = mRingOrientation.RotateVector(FVector3d(0.0, 1.0, 0.0));

    UStaticMesh* mesh = LoadObject<UStaticMesh>(nullptr, SOL::Paths::BODY_MESH);
    UMaterialInterface* material = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::BODY_MATERIAL);
    UNiagaraSystem* farFieldSystem = LoadObject<UNiagaraSystem>(nullptr, SOL::Paths::RING_FAR_NIAGARA_SYSTEM);
    UNiagaraSystem* denseSystem = LoadObject<UNiagaraSystem>(nullptr, SOL::Paths::RING_DENSE_NIAGARA_SYSTEM);
    if (mesh == nullptr || material == nullptr || farFieldSystem == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("RingVisuals %s: failed to load %s, %s or %s"), *GetName(), SOL::Paths::BODY_MESH,
            SOL::Paths::BODY_MATERIAL, SOL::Paths::RING_FAR_NIAGARA_SYSTEM);
        return;
    }
    // The dense rock layer is an optional extra: without its asset or the sim clock the ring still draws, minus it
    mIsDenseEnabled = denseSystem != nullptr && SimClock != nullptr;
    if (!mIsDenseEnabled)
    {
        UE_LOG(LogSOL, Warning, TEXT("RingVisuals %s: dense rock layer disabled (%s or the sim clock is missing)"),
            *GetName(), SOL::Paths::RING_DENSE_NIAGARA_SYSTEM);
    }

    // Far-field: one shared system, parameterized per instance (SDD 6 Amendment 11)
    FarFieldNiagara->SetAsset(farFieldSystem);
    FarFieldNiagara->SetVariableFloat(SOL::RingFarFieldParams::OUTER_RADIUS_CM,
        static_cast<float>(mRingDef.OuterRadiusM * SOL::METERS_TO_CM));
    FarFieldNiagara->SetVariableFloat(SOL::RingFarFieldParams::INNER_RADIUS_CM,
        static_cast<float>(mRingDef.InnerRadiusM * SOL::METERS_TO_CM));
    FarFieldNiagara->SetVariableLinearColor(SOL::RingFarFieldParams::COLOR, mRingDef.Color);
    // The far field is one analytic-profile disc since SDD 6 Amendment 15 (no sprite count/size parameters); these
    // optional parameters pick the ring's real radial profile and feed the planet's shadow
    FarFieldNiagara->SetVariableInt(SOL::RingFarDiscParams::PROFILE_INDEX, ProfileIndexForPlanet(PlanetName));
    FarFieldNiagara->SetVariableFloat(SOL::RingFarDiscParams::PLANET_RADIUS_CM,
        static_cast<float>(registry.GetRadiusM(mPlanetIndex) * SOL::METERS_TO_CM));

    const TArray<FSOLRadiusBandM> gapBands = BuildRingGapBandsM(registry, mRingDef);
    if (gapBands.Num() > SOL::RING_FAR_FIELD_GAP_SLOT_COUNT)
    {
        UE_LOG(LogSOL, Warning, TEXT("RingVisuals %s: %d gap bands, only %d far-field slots - the rest render as solid"),
            *GetName(), gapBands.Num(), SOL::RING_FAR_FIELD_GAP_SLOT_COUNT);
    }
    // Every slot's full parameter name is a real SOLConstants.h constant (not built from a prefix), so this array
    // literally cannot desync from SOL::RING_FAR_FIELD_GAP_SLOT_COUNT without a compile error at the static_assert
    static_assert(SOL::RING_FAR_FIELD_GAP_SLOT_COUNT == 3, "Add/remove a {inner, outer} pair below to match");
    const TCHAR* const gapInnerNames[SOL::RING_FAR_FIELD_GAP_SLOT_COUNT] = { SOL::RingFarFieldParams::GAP0_INNER_RADIUS_CM,
        SOL::RingFarFieldParams::GAP1_INNER_RADIUS_CM, SOL::RingFarFieldParams::GAP2_INNER_RADIUS_CM };
    const TCHAR* const gapOuterNames[SOL::RING_FAR_FIELD_GAP_SLOT_COUNT] = { SOL::RingFarFieldParams::GAP0_OUTER_RADIUS_CM,
        SOL::RingFarFieldParams::GAP1_OUTER_RADIUS_CM, SOL::RingFarFieldParams::GAP2_OUTER_RADIUS_CM };
    for (int32 slot = 0; slot < SOL::RING_FAR_FIELD_GAP_SLOT_COUNT; ++slot)
    {
        // Unused slots get (0,0): never satisfied by a real ring particle (all at radius > 0), so self-disabling -
        // same convention Amendment 11 established for the asset's own default values
        const bool bHasGap = slot < gapBands.Num();
        const double innerM = bHasGap ? gapBands[slot].LowM : 0.0;
        const double outerM = bHasGap ? gapBands[slot].HighM : 0.0;
        FarFieldNiagara->SetVariableFloat(gapInnerNames[slot], static_cast<float>(innerM * SOL::METERS_TO_CM));
        FarFieldNiagara->SetVariableFloat(gapOuterNames[slot], static_cast<float>(outerM * SOL::METERS_TO_CM));
    }
    FarFieldNiagara->Activate(true);

    // Dense rock layer: the same ring geometry plus its window parameters; activated once, on its first show, then only
    // shown/hidden and resumed/paused (UpdateDenseLayer); Deactivate is never used, as reactivating respawns all 1M
    if (mIsDenseEnabled)
    {
        DenseNiagara->SetAsset(denseSystem);
        DenseNiagara->SetVariableFloat(SOL::RingFarFieldParams::OUTER_RADIUS_CM,
            static_cast<float>(mRingDef.OuterRadiusM * SOL::METERS_TO_CM));
        DenseNiagara->SetVariableFloat(SOL::RingFarFieldParams::INNER_RADIUS_CM,
            static_cast<float>(mRingDef.InnerRadiusM * SOL::METERS_TO_CM));
        DenseNiagara->SetVariableLinearColor(SOL::RingFarFieldParams::COLOR, mRingDef.Color);
        for (int32 slot = 0; slot < SOL::RING_FAR_FIELD_GAP_SLOT_COUNT; ++slot)
        {
            const bool bHasGap = slot < gapBands.Num();
            DenseNiagara->SetVariableFloat(gapInnerNames[slot],
                static_cast<float>((bHasGap ? gapBands[slot].LowM : 0.0) * SOL::METERS_TO_CM));
            DenseNiagara->SetVariableFloat(gapOuterNames[slot],
                static_cast<float>((bHasGap ? gapBands[slot].HighM : 0.0) * SOL::METERS_TO_CM));
        }
        DenseNiagara->SetVariableInt(SOL::RingDenseParams::PARTICLE_COUNT, SOL::RING_DENSE_PARTICLE_COUNT);
        DenseNiagara->SetVariableFloat(SOL::RingDenseParams::WINDOW_RADIUS_CM, static_cast<float>(SOL::RING_DENSE_WINDOW_RADIUS_CM));
        DenseNiagara->SetVariableFloat(SOL::RingDenseParams::HALF_THICKNESS_CM, static_cast<float>(SOL::RING_DENSE_HALF_THICKNESS_CM));
        DenseNiagara->SetVariableFloat(SOL::RingDenseParams::ROCK_MIN_RADIUS_CM, static_cast<float>(SOL::RING_DENSE_ROCK_MIN_RADIUS_CM));
        DenseNiagara->SetVariableFloat(SOL::RingDenseParams::ROCK_MAX_RADIUS_CM, static_cast<float>(SOL::RING_DENSE_ROCK_MAX_RADIUS_CM));
        DenseNiagara->SetVariableFloat(SOL::RingDenseParams::CULL_ANGULAR_RADIUS, static_cast<float>(SOL::RING_DENSE_CULL_ANGULAR_RADIUS));
        DenseNiagara->SetVariableFloat(SOL::RingDenseParams::FILL, 0.0f);
        // Not activated yet: the first show activates it (UpdateDenseLayer), so a ring the player never nears never
        // spawns or keeps resident its million-particle buffers
        DenseNiagara->SetVisibility(false);
        DenseNiagara->SetWorldScale3D(FVector::OneVector);   // Absolute scale, never changes: set once, not every frame
        mIsDenseVisible = false;
    }

    // Read-only query over this ring's pool entities, at the archetype level via the shared tag (so belt entities
    // and the three other rings' entities never enter this actor's chunk iteration by composition alone - the
    // ParentBodyIndex check in ProcessChunk still separates this ring from the other three, see the class comment)
    mEntityManager = massEntities->GetMutableEntityManager().AsShared();
    mQuery.Initialize(mEntityManager.ToSharedRef());
    mQuery.AddRequirement<FSOLMinorBodyRenderFragment>(EMassFragmentAccess::ReadOnly);
    mQuery.AddRequirement<FSOLMinorBodyStateFragment>(EMassFragmentAccess::ReadOnly);
    mQuery.AddRequirement<FSOLMinorBodyOrbitFragment>(EMassFragmentAccess::ReadOnly);
    mQuery.AddTagRequirement<FSOLRingRockTag>(EMassFragmentPresence::All);
    mQuery.SetParallelCommandBufferEnabled(false);

    // Scratch transforms sized once to this ring's fixed pool size; every frame overwrites them in place. Zero
    // scale (not Identity) so any slot ProcessChunk never writes this frame (should not happen, but costs nothing
    // to be defensive) stays invisible rather than showing a unit-scale sphere at the Unreal origin
    mScratchTransforms.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector),
        SOLRingPatch::PoolEntityCountPerRing());
    FillTransforms();

    // One placeholder material instance and one bulk instance add (the per-frame update only moves them)
    UMaterialInstanceDynamic* nearFieldMaterial = UMaterialInstanceDynamic::Create(material, this);
    nearFieldMaterial->SetVectorParameterValue(FName(SOL::BodyMaterialParams::COLOR_A), mRingDef.Color);
    nearFieldMaterial->SetVectorParameterValue(FName(SOL::BodyMaterialParams::COLOR_B), mRingDef.Color);
    nearFieldMaterial->SetScalarParameterValue(FName(SOL::BodyMaterialParams::SUN_ILLUMINANCE), SOL::SUN_ILLUMINANCE_LUX);
    NearFieldMesh->SetStaticMesh(mesh);
    NearFieldMesh->SetMaterial(0, nearFieldMaterial);
    NearFieldMesh->SetWorldTransform(FTransform::Identity);
    if (!mScratchTransforms.IsEmpty())
    {
        NearFieldMesh->AddInstances(mScratchTransforms, false, false, false);
    }

    mUniverseUpdatedHandle = AnchorSubsystem->OnUniverseUpdated().AddUObject(this, &ASOLRingVisuals::HandleUniverseUpdated);
    UE_LOG(LogSOL, Log, TEXT("RingVisuals %s: %s ring, %d near-field instances"), *GetName(),
        *PlanetName.ToString(), mScratchTransforms.Num());
}

//////////////////////////////////////////////////////////////////////////
// Unsubscribes from universe updates
void ASOLRingVisuals::EndPlay(const EEndPlayReason::Type endPlayReason)
{
    if (AnchorSubsystem != nullptr)
    {
        AnchorSubsystem->OnUniverseUpdated().Remove(mUniverseUpdatedHandle);
    }
    mUniverseUpdatedHandle.Reset();
    mEntityManager.Reset();
    Super::EndPlay(endPlayReason);
}

//////////////////////////////////////////////////////////////////////////
// Snapshots the render origin/viewpoint and this frame's near-field alpha once, re-places the far field, and -
// unless the near field was already fully faded and still is - runs the Mass query to write every active entity's
// near-field transform. Returns whether the near field was (re)computed this frame
bool ASOLRingVisuals::FillTransforms()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLRingVisuals::FillTransforms);

    mFrameRenderOrigin = AnchorSubsystem->GetRenderOrigin();
    mFrameViewpointM = AnchorSubsystem->GetViewpointM();
    mFrameNearFieldAlpha = SOLRingPatch::ComputeNearFieldAlpha(
        DistanceOutsideRingVolumeM(AnchorSubsystem->GetObserverPositionM()), SOLRingPatch::ActivationMarginM());

    // Cheap early-out: once fully faded and still fully faded, every slot is already zero-scaled from the last
    // real pass, so skip the (relatively expensive, up to ~10,000-entity) query and leave mScratchTransforms alone
    const bool bNeedsNearFieldUpdate = mFrameNearFieldAlpha > 0.0 || mLastNearFieldAlpha > 0.0;
    if (bNeedsNearFieldUpdate)
    {
        FMassExecutionContext context = mEntityManager->CreateExecutionContext(0.0f);
        mQuery.ParallelForEachEntityChunk(context, mChunkFunction);
    }
    mLastNearFieldAlpha = mFrameNearFieldAlpha;

    PlaceFarFieldNiagara();
    UpdateFarFieldLighting();
    UpdateDenseLayer();
    return bNeedsNearFieldUpdate;
}

//////////////////////////////////////////////////////////////////////////
// Pushes the far field's Sun direction (unit vector toward the Sun, in the disc's own local axes) when it changes
void ASOLRingVisuals::UpdateFarFieldLighting()
{
    if (mSunIndex == INDEX_NONE)
    {
        return;
    }
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    const FVector3d towardSunEcliptic = registry.GetPositionM(mSunIndex) - registry.GetPositionM(mPlanetIndex);
    if (towardSunEcliptic.IsNearlyZero())
    {
        return;
    }
    // Ecliptic -> ring-local (inverse of the host orientation) -> the Niagara disc's left-handed local axes (Y mirrored)
    const FVector3d ringLocal = mRingOrientation.UnrotateVector(towardSunEcliptic.GetSafeNormal());
    const FVector3d niagaraLocal = SOLRingDense::RingFrameToNiagaraLocalCm(ringLocal);
    // The direction changes over hours, not frames: skip the Niagara write unless it moved by about 0.01 degrees
    if ((niagaraLocal - mLastSunDirection).SizeSquared() < 1.0e-8)
    {
        return;
    }
    mLastSunDirection = niagaraLocal;
    FarFieldNiagara->SetVariableVec3(SOL::RingFarDiscParams::SUN_DIRECTION, FVector(niagaraLocal));
}

//////////////////////////////////////////////////////////////////////////
// Shows, places and drives the dense rock layer while the render viewpoint is near the ring at a low time warp, and
// hides and pauses it otherwise
void ASOLRingVisuals::UpdateDenseLayer()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLRingVisuals::UpdateDenseLayer);
    if (!mIsDenseEnabled)
    {
        return;
    }

    // The layer is drawn around the render viewpoint (the map camera in map mode, not the ship), so that is also what
    // decides whether it is near enough to the ring to be worth simulating
    const double denseAlpha = SOLRingDense::ComputeDenseAlpha(DistanceOutsideRingVolumeM(mFrameViewpointM),
        SOL::RING_DENSE_FADE_IN_DISTANCE_M, SOL::RING_DENSE_FADE_OUT_DISTANCE_M);
    const double secondsSinceJ2000 = SimClock->GetClock().GetSecondsSinceJ2000();
    const bool bShow = SOLRingDense::ShouldShowDenseTier(denseAlpha, SimClock->GetClock().GetWarpFactor(),
        SOL::RING_DENSE_MAX_TIME_WARP);

    if (bShow != mIsDenseVisible)
    {
        mIsDenseVisible = bShow;
        mDenseHiddenSinceSeconds = -1.0;
        if (bShow && (!mIsDenseActivated || !DenseNiagara->IsActive()))
        {
            if (mIsDenseActivated && !mIsDenseReleased)
            {
                // The engine deactivated the system behind our back (scalability or culling policy); bring it back,
                // at the price of respawning its rocks, rather than staying silently empty for the rest of the session
                UE_LOG(LogSOL, Warning, TEXT("RingVisuals %s: dense rock layer was deactivated by the engine; reactivating"),
                    *GetName());
            }
            DenseNiagara->Activate(true);
            mIsDenseActivated = true;
            mIsDenseReleased = false;
        }
        DenseNiagara->SetVisibility(bShow);
        DenseNiagara->SetPaused(!bShow);
        UE_LOG(LogSOL, Log, TEXT("RingVisuals %s: dense rock layer %s (alpha %.3f)"), *GetName(),
            bShow ? TEXT("shown") : TEXT("hidden"), denseAlpha);
    }
    if (!bShow)
    {
        // Stay in step with the sim clock so the first visible frame's angle step is one frame, not the whole absence
        mLastSecondsSinceJ2000 = secondsSinceJ2000;

        // Once hidden for a while, free the rock buffers (about 214 MB of VRAM); only the ring the player is at is
        // ever shown, so this caps the layer at about one ring's buffers. Re-showing respawns the rocks.
        const double nowSeconds = GetWorld()->GetTimeSeconds();
        if (mIsDenseActivated && !mIsDenseReleased)
        {
            if (mDenseHiddenSinceSeconds < 0.0)
            {
                mDenseHiddenSinceSeconds = nowSeconds;
            }
            else if (nowSeconds - mDenseHiddenSinceSeconds >= SOL::RING_DENSE_RELEASE_DELAY_SECONDS)
            {
                DenseNiagara->DeactivateImmediate();
                mIsDenseReleased = true;
            }
        }
        return;
    }
    mDenseHiddenSinceSeconds = -1.0;

    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    const FVector3d relativeM = mFrameViewpointM - registry.GetPositionM(mPlanetIndex);
    const double planeDistanceM = FVector3d::DotProduct(relativeM, mRingNormal);
    const double planeRadiusM = FMath::Sqrt(FMath::Max(relativeM.SizeSquared() - planeDistanceM * planeDistanceM, 0.0));

    // Integrating (not omega * t) means a radial move, which changes omega, never makes the carpet jump
    mDenseSpinAngleRad = SOLRingDense::AdvanceSpinAngle(mDenseSpinAngleRad, mPlanetGM, planeRadiusM,
        secondsSinceJ2000 - mLastSecondsSinceJ2000);
    mLastSecondsSinceJ2000 = secondsSinceJ2000;

    // Camera position in the ring's co-rotating frame (right-handed), then in the Niagara component's local axes, which
    // are that frame mirrored in Y because the component's rotation is converted to Unreal's left-handed axes; the
    // window phase and centre must be in the component's own axes for the carpet to stay fixed in the ring frame
    const FVector3d ringFrameCm = SOLRingDense::RingFramePositionCm(relativeM, mRingAxisE1, mRingAxisE2, mRingNormal,
        mDenseSpinAngleRad);
    const FVector3d localCm = SOLRingDense::RingFrameToNiagaraLocalCm(ringFrameCm);

    // The carpet's local frame is the ring frame spun by the same angle, converted to Unreal's axes like the far field
    const FQuat4d spunOrientation = mRingOrientation * FQuat4d(FVector3d(0.0, 0.0, 1.0), mDenseSpinAngleRad);
    DenseNiagara->SetWorldLocationAndRotation(FVector(mFrameRenderOrigin.UniverseToRenderCm(mFrameViewpointM)),
        FQuat(SOLRender::EclipticToUnreal(spunOrientation)));

    const double windowRadiusCm = SOL::RING_DENSE_WINDOW_RADIUS_CM;
    DenseNiagara->SetVariableVec3(SOL::RingDenseParams::WINDOW_PHASE_CM, FVector(
        SOLRingDense::WindowPhaseCm(localCm.X, windowRadiusCm), SOLRingDense::WindowPhaseCm(localCm.Y, windowRadiusCm),
        localCm.Z));
    // Kilometres, converted in double before the cast: the system only masks with this value, and a vec3 beyond about
    // 10 km makes Niagara log a precision warning every call (the centre is ~1e10 cm from the planet)
    constexpr double centimetersPerKilometer = SOL::METERS_TO_CM * SOL::METERS_PER_KM;
    DenseNiagara->SetVariableVec3(SOL::RingDenseParams::WINDOW_CENTER_KM, FVector(FVector3f(localCm / centimetersPerKilometer)));
    DenseNiagara->SetVariableFloat(SOL::RingDenseParams::FILL,
        static_cast<float>(SOLRingDense::DenseFillFraction(mRingDef.Density) * denseAlpha));
}

//////////////////////////////////////////////////////////////////////////
// Re-places and rescales the far-field Niagara component from this frame's render-origin snapshot
void ASOLRingVisuals::PlaceFarFieldNiagara()
{
    const FVector3d planetPositionM = BodyRegistry->GetRegistry().GetPositionM(mPlanetIndex);

    // BodyPlacement log-compresses both location and radius by the same factor past DEFAULT_MAX_RENDER_DISTANCE_CM
    // (see SOLRender::ComputePlacement); passing the ring's real OuterRadiusM (not 0) gives back that factor as
    // placement.RadiusCm, which becomes this component's uniform scale, so the annulus - baked into the Niagara
    // system at its true centimeter size - shrinks with distance exactly like a rendered body's mesh does instead
    // of staying true-to-life size on screen (review-caught: a far-but-still-visible ring was ~770x too big at
    // Earth-Saturn distance before this fix, since nothing previously scaled the component at all)
    const FSOLRenderPlacement placement = mFrameRenderOrigin.BodyPlacement(planetPositionM, mFrameViewpointM,
        mRingDef.OuterRadiusM, SOL::DEFAULT_MAX_RENDER_DISTANCE_CM);
    const double outerRadiusCm = mRingDef.OuterRadiusM * SOL::METERS_TO_CM;
    const double scale = outerRadiusCm > 0.0 ? placement.RadiusCm / outerRadiusCm : 1.0;
    FarFieldNiagara->SetWorldLocationAndRotation(FVector(placement.LocationCm), mFarFieldRotation);
    FarFieldNiagara->SetWorldScale3D(FVector(scale));
}

//////////////////////////////////////////////////////////////////////////
// Writes the near-field transforms of one chunk's entities that belong to this ring: another ring's entity (same
// shared tag) is skipped outright, and an inactive entity or a fully-faded ring is scaled to zero rather than
// placed, so it renders nothing without needing a separate "hidden" representation
void ASOLRingVisuals::ProcessChunk(FMassExecutionContext& context)
{
    const TConstArrayView<FSOLMinorBodyRenderFragment> renders = context.GetFragmentView<FSOLMinorBodyRenderFragment>();
    const TConstArrayView<FSOLMinorBodyStateFragment> states = context.GetFragmentView<FSOLMinorBodyStateFragment>();
    const TConstArrayView<FSOLMinorBodyOrbitFragment> orbits = context.GetFragmentView<FSOLMinorBodyOrbitFragment>();

    FTransform* const transforms = mScratchTransforms.GetData();
    const int32 instanceCount = mScratchTransforms.Num();
    const int32 entityCount = context.GetNumEntities();
    const float alphaScale = static_cast<float>(mFrameNearFieldAlpha);
    for (int32 entity = 0; entity < entityCount; ++entity)
    {
        if (orbits[entity].ParentBodyIndex != mPlanetIndex)
        {
            continue; // Another ring's entity (same shared FSOLRingRockTag); not this actor's to place
        }
        const int32 instance = renders[entity].InstanceIndex;
        if (instance < 0 || instance >= instanceCount)
        {
            continue;
        }
        if (!renders[entity].bActive || alphaScale <= 0.0f)
        {
            transforms[instance] = FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
            continue;
        }
        const FSOLRenderPlacement placement = mFrameRenderOrigin.BodyPlacement(states[entity].PositionM,
            mFrameViewpointM, renders[entity].RadiusM, SOL::DEFAULT_MAX_RENDER_DISTANCE_CM);
        transforms[instance] = FTransform(FQuat::Identity, FVector(placement.LocationCm),
            FVector(placement.RadiusCm / SOL::BODY_MESH_RADIUS_CM * alphaScale));
    }
}

//////////////////////////////////////////////////////////////////////////
// Re-places the far field (always) and every near-field instance (only if FillTransforms actually recomputed
// them this frame), refreshing the cross-fade weight for the frame
void ASOLRingVisuals::HandleUniverseUpdated()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLRingVisuals::HandleUniverseUpdated);
    if (!mEntityManager.IsValid())
    {
        return;
    }

    // Local space equals world space (the component is pinned at the origin), so no per-instance inverse transform
    if (FillTransforms() && !mScratchTransforms.IsEmpty())
    {
        // bMarkRenderStateDirty=false: the instance-data manager already pushes these transforms incrementally;
        // true here would rebuild the ISM's scene proxy from scratch every frame (same reasoning as the belt's)
        NearFieldMesh->BatchUpdateInstancesTransforms(0, mScratchTransforms, false, false, true);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns how far worldPositionM is outside this ring's physical [InnerRadiusM, OuterRadiusM) band and
// SOLRingPatch::HalfThicknessM() slab (0 if inside both), mirroring SOLRingPatch::ComputeActiveCell's own
// band/thickness test but as a continuous distance rather than a discrete admit/reject
double ASOLRingVisuals::DistanceOutsideRingVolumeM(const FVector3d& worldPositionM) const
{
    const FVector3d planetPositionM = BodyRegistry->GetRegistry().GetPositionM(mPlanetIndex);
    const FVector3d relativeM = worldPositionM - planetPositionM;
    const double planeDistanceM = FVector3d::DotProduct(relativeM, mRingNormal);
    const double radiusM = FMath::Sqrt(FMath::Max(relativeM.SizeSquared() - planeDistanceM * planeDistanceM, 0.0));
    const double radialExcessM =
        FMath::Max(FMath::Max(mRingDef.InnerRadiusM - radiusM, radiusM - mRingDef.OuterRadiusM), 0.0);
    const double verticalExcessM = FMath::Max(FMath::Abs(planeDistanceM) - SOLRingPatch::HalfThicknessM(), 0.0);
    return FMath::Sqrt(radialExcessM * radialExcessM + verticalExcessM * verticalExcessM);
}
