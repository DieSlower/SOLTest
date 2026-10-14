/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "OrbitLines/SOLOrbitLineVisuals.h"

#include "Game/SOLGameMode.h"
#include "Menu/SOLMenuState.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "OrbitLines/SOLOrbitLineComponent.h"
#include "OrbitLines/SOLOrbitLineLogic.h"
#include "OrbitLines/SOLOrbitLineMesh.h"
#include "SOLTest.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

namespace
{
    // Developer A/B switch for GPU measurements (SDD 17 Amendment 1); the player's toggle is the O key
    TAutoConsoleVariable<int32> CVarSOLOrbitLines(TEXT("sol.OrbitLines"), 1,
        TEXT("1: world-space orbit lines drawn while toggled on; 0: never drawn (A/B profiling, SDD 17)"));

    // Custom primitive data slots beyond SOLOrbitLineMesh::PackCustomData's (SOL::ORBIT_LINE_CUSTOM_DATA_TOTAL_FLOATS)
    constexpr int32 CUSTOM_DATA_ROTATION_SLOT = SOL::ORBIT_LINE_CUSTOM_DATA_FLOATS;
    constexpr int32 CUSTOM_DATA_SHARED_PARENT_SLOT = SOL::ORBIT_LINE_CUSTOM_DATA_FLOATS + 4;

    //////////////////////////////////////////////////////////////////////////
    // Converts a vector to a collection vector (w as given)
    FLinearColor OrbitLineToColor(const FVector3f& value, const float w)
    {
        return FLinearColor(value.X, value.Y, value.Z, w);
    }
}

//////////////////////////////////////////////////////////////////////////
// Creates the root; no tick (updates run on USOLAnchorSubsystem::OnUniverseUpdated)
ASOLOrbitLineVisuals::ASOLOrbitLineVisuals()
{
    PrimaryActorTick.bCanEverTick = false;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

//////////////////////////////////////////////////////////////////////////
// Shows or hides every orbit line, the targeted one included
void ASOLOrbitLineVisuals::SetLinesEnabled(const bool bEnabled)
{
    mIsEnabled = bEnabled;
    mIsDrawn = ShouldDrawLines();
    SetActorHiddenInGame(!mIsDrawn);
    if (!bEnabled)
    {
        mIsTransientShown = false;
        if (TransientComponent != nullptr)
        {
            TransientComponent->SetVisibility(false);
        }
        mHighlightedSlot = INDEX_NONE;
    }
    UE_LOG(LogSOL, Log, TEXT("OrbitLineVisuals %s: orbit lines %s"), *GetName(), bEnabled ? TEXT("ON") : TEXT("OFF"));
}

//////////////////////////////////////////////////////////////////////////
// Flips the lines: through the settings subsystem when there is one (saved at the next write point), else locally
void ASOLOrbitLineVisuals::ToggleLines()
{
    const bool bShown = !mIsEnabled;
    if (Settings != nullptr && !mIsVerificationOverride)
    {
        Settings->SetOrbitLinesShown(bShown);
    }
    SetLinesEnabled(bShown);
}

//////////////////////////////////////////////////////////////////////////
// Detaches the lines from the settings for a verification run and starts them on
void ASOLOrbitLineVisuals::BeginVerificationOverride()
{
    mIsVerificationOverride = true;
    SetLinesEnabled(true);
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of line components currently visible (verification)
int32 ASOLOrbitLineVisuals::CountVisibleLines() const
{
    if (IsHidden())
    {
        return 0;
    }
    int32 count = 0;
    for (const USOLOrbitLineComponent* component : LineComponents)
    {
        count += component->IsVisible() ? 1 : 0;
    }
    return count + (mIsTransientShown ? 1 : 0);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the lines should be drawn now (setting on, main menu not showing, sol.OrbitLines on)
bool ASOLOrbitLineVisuals::ShouldDrawLines() const
{
    const bool bInMainMenu = Menu != nullptr && Menu->GetState().Mode == SOLMenuState::EMenuMode::MainMenu;
    return SOLOrbitLineLogic::AreLinesVisible(mIsEnabled, bInMainMenu)
        && CVarSOLOrbitLines.GetValueOnGameThread() != 0;
}

//////////////////////////////////////////////////////////////////////////
// Resolves subsystems and assets, builds every permanent line and subscribes to universe updates
void ASOLOrbitLineVisuals::BeginPlay()
{
    Super::BeginPlay();

    // The persisted choice (SDD 17, 17d) decides the starting state; without settings (tests) the lines start on
    UWorld* world = GetWorld();
    Settings = USOLSettingsSubsystem::Get(this);
    if (Settings != nullptr)
    {
        mIsEnabled = Settings->AreOrbitLinesShown();
    }

    // Outside a SOL game world (editor previews, bare test worlds) there is no universe to draw lines for
    if (!ASOLGameMode::IsSOLGameWorld(world))
    {
        return;
    }
    AnchorSubsystem = world->GetSubsystem<USOLAnchorSubsystem>();
    BodyRegistry = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    SimClock = world->GetSubsystem<USOLSimClockSubsystem>();
    Targeting = world->GetSubsystem<USOLTargetingSubsystem>();
    Menu = world->GetSubsystem<USOLMenuSubsystem>();
    LineMaterial = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::ORBIT_LINE_MATERIAL);
    ParameterCollection = LoadObject<UMaterialParameterCollection>(nullptr,
        SOL::Paths::ORBIT_LINE_PARAMETER_COLLECTION);
    if (AnchorSubsystem == nullptr || BodyRegistry == nullptr || SimClock == nullptr || LineMaterial == nullptr
        || ParameterCollection == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("OrbitLineVisuals %s: subsystems or %s / %s missing; no orbit lines"), *GetName(),
            SOL::Paths::ORBIT_LINE_MATERIAL, SOL::Paths::ORBIT_LINE_PARAMETER_COLLECTION);
        return;
    }
    ParameterCollectionInstance = world->GetParameterCollectionInstance(ParameterCollection);

    // Style tunables go to the collection once, so the shader holds no copy of them
    mFrameParamName = FName(SOL::OrbitLineMpcParams::FRAME);
    mStyle2ParamName = FName(SOL::OrbitLineMpcParams::STYLE2);
    mSharedParentHighParamName = FName(SOL::OrbitLineMpcParams::SHARED_PARENT_HIGH);
    mSharedParentLowParamName = FName(SOL::OrbitLineMpcParams::SHARED_PARENT_LOW);
    for (int32 index = 0; index < SOL::ORBIT_LINE_MAX_OCCLUDERS; ++index)
    {
        mOccluderParamNames[index] = FName(SOL::OrbitLineMpcParams::OCCLUDERS[index]);
    }
    ParameterCollectionInstance->SetVectorParameterValue(FName(SOL::OrbitLineMpcParams::STYLE), FLinearColor(
        static_cast<float>(SOL::ORBIT_LINE_WIDTH_PX), static_cast<float>(SOL::ORBIT_LINE_DASH_DUTY),
        static_cast<float>(SOL::ORBIT_LINE_FADE_START_SMA), static_cast<float>(SOL::ORBIT_LINE_FADE_END_SMA)));
    ParameterCollectionInstance->SetVectorParameterValue(FName(SOL::OrbitLineMpcParams::DASH), FLinearColor(
        static_cast<float>(SOL::ORBIT_LINE_DASH_MIN_PERIOD_PX), static_cast<float>(SOL::ORBIT_LINE_DASH_MAX_LEVEL),
        static_cast<float>(SOL::ORBIT_LINE_DASH_SOLID_START_PX),
        static_cast<float>(SOL::ORBIT_LINE_DASH_SOLID_END_PX)));

    // Line selection: registry bodies with a parent, then the largest named asteroids
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    mSunIndex = registry.FindByName(FName(SOL::BodyNames::SUN));
    mRegistryParents.Reserve(registry.Num());
    mRegistryRadiiM.Reserve(registry.Num());
    for (int32 index = 0; index < registry.Num(); ++index)
    {
        mRegistryParents.Add(registry.GetParent(index));
        mRegistryRadiiM.Add(registry.GetRadiusM(index));
    }
    mAsteroids = SOLAsteroidBelt::RealAsteroids();
    TArray<double> minorRadii;
    TArray<bool> minorNamed;
    minorRadii.Reserve(mAsteroids.Num());
    minorNamed.Reserve(mAsteroids.Num());
    for (const FSOLAsteroidDef& asteroid : mAsteroids)
    {
        minorRadii.Add(asteroid.RadiusM);
        minorNamed.Add(!asteroid.Name.IsNone());
    }
    SOLOrbitLineMath::BuildPermanentLines(mRegistryParents, minorRadii, minorNamed,
        SOL::ORBIT_LINE_NAMED_MINOR_COUNT, mLineKeys);
    mOccluderIndices.Reserve(registry.Num());

    // Build every line's mesh once; a key without a drawable orbit keeps an empty, never-drawn component
    const double startSeconds = FPlatformTime::Seconds();
    mLines.Reserve(mLineKeys.Num());
    LineComponents.Reserve(mLineKeys.Num());
    LineMeshes.Reserve(mLineKeys.Num());
    for (const SOLOrbitLineMath::FLineKey& key : mLineKeys)
    {
        FLineState line;
        line.Key = key;
        FSOLKeplerElements elements;
        const bool bDrawable = ResolveLineSource(key, elements, line.ParentIndex, line.BodyRadiusM);
        line.bUsesSharedParent = key.Source == SOLOrbitLineMath::ELineSource::MinorBody;
        UStaticMesh* mesh = nullptr;
        USOLOrbitLineComponent* component = CreateLineComponent(mesh);
        line.VertexCount = bDrawable ? BuildLineMesh(elements, line.BodyRadiusM, mesh) : 0;
        line.BuiltElements = elements;
        line.bIsShown = line.VertexCount > 0;
        mTotalVertexCount += line.VertexCount;
        component->SetStaticMesh(mesh);
        component->SetVisibility(line.bIsShown);
        LineComponents.Add(component);
        LineMeshes.Add(mesh);
        mLines.Add(line);
    }
    UStaticMesh* transientMesh = nullptr;
    TransientComponent = CreateLineComponent(transientMesh);
    TransientMesh = transientMesh;
    TransientComponent->SetVisibility(false);
    mNeedsRebuild.Init(false, mLines.Num());
    mRebuildSlots.Reserve(SOL::ORBIT_LINE_MAX_REBUILDS_PER_FRAME);
    SetLinesEnabled(mIsEnabled);

    mUniverseUpdatedHandle = AnchorSubsystem->OnUniverseUpdated().AddUObject(this,
        &ASOLOrbitLineVisuals::HandleUniverseUpdated);
    UE_LOG(LogSOL, Log, TEXT("OrbitLineVisuals %s: %d lines, %d vertices, built in %.1f ms (ribbons %.1f ms, render "
        "data %.1f ms)"), *GetName(), mLines.Num(), mTotalVertexCount,
        (FPlatformTime::Seconds() - startSeconds) * 1000.0, mRibbonSeconds * 1000.0, mRenderDataSeconds * 1000.0);
}

//////////////////////////////////////////////////////////////////////////
// Unsubscribes from universe updates
void ASOLOrbitLineVisuals::EndPlay(const EEndPlayReason::Type endPlayReason)
{
    if (AnchorSubsystem != nullptr)
    {
        AnchorSubsystem->OnUniverseUpdated().Remove(mUniverseUpdatedHandle);
    }
    mUniverseUpdatedHandle.Reset();
    Super::EndPlay(endPlayReason);
}

//////////////////////////////////////////////////////////////////////////
// Resolves a key's current elements, parent and body radius; false if it has no drawable orbit
bool ASOLOrbitLineVisuals::ResolveLineSource(const SOLOrbitLineMath::FLineKey& key, FSOLKeplerElements& outElements,
    int32& outParentIndex, double& outRadiusM) const
{
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    const double centuries = SimClock->GetClock().GetCenturiesSinceJ2000();
    if (key.Source == SOLOrbitLineMath::ELineSource::RegistryBody && key.Index >= 0 && key.Index < registry.Num())
    {
        outParentIndex = registry.GetParent(key.Index);
        outRadiusM = registry.GetRadiusM(key.Index);
        outElements = registry.GetElements(key.Index).AtCenturies(centuries);
    }
    else if (key.Source == SOLOrbitLineMath::ELineSource::MinorBody && mAsteroids.IsValidIndex(key.Index))
    {
        outParentIndex = mSunIndex;
        outRadiusM = mAsteroids[key.Index].RadiusM;
        outElements = mAsteroids[key.Index].Elements.AtCenturies(centuries);
    }
    else
    {
        return false;
    }
    return outParentIndex != INDEX_NONE && SOLOrbitLineMath::IsDrawableOrbit(outElements);
}

//////////////////////////////////////////////////////////////////////////
// Samples the elements and (re)builds the mesh; returns the vertex count (0 if the orbit is not drawable)
int32 ASOLOrbitLineVisuals::BuildLineMesh(const FSOLKeplerElements& elements, const double bodyRadiusM,
    UStaticMesh* mesh)
{
    mScratchPointsM.Reset();
    mScratchTangents.Reset();
    mScratchArcLengthsM.Reset();
    const int32 segments = SOLOrbitLineMath::SegmentCountFor(elements.SemiMajorAxisM, bodyRadiusM);
    if (SOLOrbitLineMath::SampleOrbit(elements, segments, mScratchPointsM, mScratchTangents, mScratchArcLengthsM) == 0)
    {
        return 0;
    }
    const double ribbonStartSeconds = FPlatformTime::Seconds();
    if (!SOLOrbitLineMesh::BuildRibbon(mScratchPointsM, mScratchTangents, mScratchArcLengthsM,
        SOL::ORBIT_LINE_DASH_COUNT, mScratchVertices, mScratchIndices))
    {
        return 0;
    }
    const double renderDataStartSeconds = FPlatformTime::Seconds();
    mRibbonSeconds += renderDataStartSeconds - ribbonStartSeconds;
    SOLOrbitLineMesh::ApplyRibbonToMesh(*mesh, mScratchVertices, mScratchIndices);
    mRenderDataSeconds += FPlatformTime::Seconds() - renderDataStartSeconds;
    return 2 * mScratchPointsM.Num();
}

//////////////////////////////////////////////////////////////////////////
// Creates a line component with its own mesh, registered and attached to the root
USOLOrbitLineComponent* ASOLOrbitLineVisuals::CreateLineComponent(UStaticMesh*& outMesh)
{
    outMesh = NewObject<UStaticMesh>(this, NAME_None, RF_Transient);
    outMesh->SetStaticMaterials({ FStaticMaterial(LineMaterial) });
    outMesh->bSupportRayTracing = false;
    USOLOrbitLineComponent* component = NewObject<USOLOrbitLineComponent>(this);
    component->SetupAttachment(RootComponent);
    component->SetUsingAbsoluteLocation(true);
    component->SetUsingAbsoluteRotation(true);
    component->SetUsingAbsoluteScale(true);
    component->SetWorldTransform(FTransform::Identity);
    component->SetMaterial(0, LineMaterial);
    component->RegisterComponent();
    return component;
}

//////////////////////////////////////////////////////////////////////////
// Writes the per-frame line data, the highlight, the transient line and the occluders
void ASOLOrbitLineVisuals::HandleUniverseUpdated()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLOrbitLineVisuals::HandleUniverseUpdated);

    // Follow the setting (the Settings checkbox previews live), the main menu and the developer switch
    if (Settings != nullptr && !mIsVerificationOverride && Settings->AreOrbitLinesShown() != mIsEnabled)
    {
        SetLinesEnabled(Settings->AreOrbitLinesShown());
    }
    const bool bDraw = ShouldDrawLines();
    if (bDraw != mIsDrawn)
    {
        mIsDrawn = bDraw;
        SetActorHiddenInGame(!bDraw);
    }
    if (!bDraw)
    {
        return;
    }
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    const double centuries = SimClock->GetClock().GetCenturiesSinceJ2000();
    const FVector3d& viewpointM = AnchorSubsystem->GetViewpointM();
    const FVector viewpointRenderCm = AnchorSubsystem->UniverseToRenderCm(viewpointM);
    ParameterCollectionInstance->SetVectorParameterValue(mFrameParamName, FLinearColor(
        static_cast<float>(viewpointRenderCm.X), static_cast<float>(viewpointRenderCm.Y),
        static_cast<float>(viewpointRenderCm.Z), static_cast<float>(SOL::DEFAULT_MAX_RENDER_DISTANCE_CM)));

    // Shape drift first, so a rebuilt line is drawn with its new (identity) rotation this frame
    RebuildDriftedLines(centuries);

    // The Sun's offset, shared by every asteroid line
    if (mSunIndex != INDEX_NONE)
    {
        FVector3f high;
        FVector3f low;
        SOLOrbitLineMath::SplitVector(registry.GetPositionM(mSunIndex) - viewpointM, high, low);
        ParameterCollectionInstance->SetVectorParameterValue(mSharedParentHighParamName, OrbitLineToColor(high, 0.0f));
        ParameterCollectionInstance->SetVectorParameterValue(mSharedParentLowParamName, OrbitLineToColor(low, 0.0f));
    }

    // Which line the target highlights, or whether it needs the transient line
    bool bTargetHasOrbit = false;
    const SOLOrbitLineMath::FLineKey targetKey = GetTargetKey(bTargetHasOrbit);
    const SOLOrbitLineMath::FTargetLineResult target = SOLOrbitLineMath::ResolveTargetLine(true, targetKey,
        bTargetHasOrbit, mLineKeys);
    mHighlightedSlot = target.HighlightSlot;

    // Per line: hide it once wholly faded out, else write its data (asteroid lines only when the highlight changes)
    for (int32 slot = 0; slot < mLines.Num(); ++slot)
    {
        FLineState& line = mLines[slot];
        if (line.VertexCount == 0)
        {
            continue;
        }
        const bool bHighlighted = slot == target.HighlightSlot;
        const double parentDistanceM = (registry.GetPositionM(line.ParentIndex) - viewpointM).Size();
        const bool bShown = !SOLOrbitLineLogic::IsLineBeyondFade(parentDistanceM, line.BuiltElements.SemiMajorAxisM,
            line.BuiltElements.Eccentricity, bHighlighted);
        if (bShown != line.bIsShown)
        {
            line.bIsShown = bShown;
            LineComponents[slot]->SetVisibility(bShown);
        }
        if (!bShown)
        {
            continue;
        }
        if (line.bUsesSharedParent)
        {
            if (!line.bHasWrittenData || bHighlighted != line.bWasHighlighted)
            {
                WriteLineData(*LineComponents[slot], line, viewpointM, FQuat4d::Identity, bHighlighted);
                line.bHasWrittenData = true;
                line.bWasHighlighted = bHighlighted;
            }
            continue;
        }
        const FSOLKeplerElements now = registry.GetElements(line.Key.Index).AtCenturies(centuries);
        WriteLineData(*LineComponents[slot], line, viewpointM,
            SOLOrbitLineLogic::OrbitRotationBetween(line.BuiltElements, now), bHighlighted);
    }

    // The transient line: rebuilt only when the target changes
    bool bShowTransient = false;
    if (target.bShowTransient)
    {
        if (!(mTransientLine.Key == targetKey))
        {
            FSOLKeplerElements elements;
            mTransientLine = FLineState();
            mTransientLine.Key = targetKey;
            if (ResolveLineSource(targetKey, elements, mTransientLine.ParentIndex, mTransientLine.BodyRadiusM))
            {
                mTransientLine.VertexCount = BuildLineMesh(elements, mTransientLine.BodyRadiusM, TransientMesh);
                mTransientLine.BuiltElements = elements;
                TransientComponent->SetStaticMesh(TransientMesh);
            }
        }
        bShowTransient = mTransientLine.VertexCount > 0;
        if (bShowTransient)
        {
            WriteLineData(*TransientComponent, mTransientLine, viewpointM, FQuat4d::Identity, true);
        }
    }
    if (bShowTransient != mIsTransientShown)
    {
        TransientComponent->SetVisibility(bShowTransient);
        mIsTransientShown = bShowTransient;
    }

    UpdateOccluders(viewpointM);
}

//////////////////////////////////////////////////////////////////////////
// Writes one line's custom primitive data (parent offset, rotation from its built orientation, highlight)
void ASOLOrbitLineVisuals::WriteLineData(USOLOrbitLineComponent& component, const FLineState& line,
    const FVector3d& viewpointM, const FQuat4d& rotation, const bool bHighlighted)
{
    const FVector3d parentOffsetM = line.bUsesSharedParent ? FVector3d::ZeroVector
        : BodyRegistry->GetRegistry().GetPositionM(line.ParentIndex) - viewpointM;
    const FLinearColor color = (line.Key.Source == SOLOrbitLineMath::ELineSource::MinorBody && !bHighlighted)
        ? SOL::ORBIT_LINE_MINOR_COLOR : SOLOrbitLineMath::LineColor(bHighlighted);
    SOLOrbitLineMesh::PackCustomData(parentOffsetM, line.BuiltElements.SemiMajorAxisM, bHighlighted, color,
        mCustomData);
    mCustomData[CUSTOM_DATA_ROTATION_SLOT] = static_cast<float>(rotation.X);
    mCustomData[CUSTOM_DATA_ROTATION_SLOT + 1] = static_cast<float>(rotation.Y);
    mCustomData[CUSTOM_DATA_ROTATION_SLOT + 2] = static_cast<float>(rotation.Z);
    mCustomData[CUSTOM_DATA_ROTATION_SLOT + 3] = static_cast<float>(rotation.W);
    mCustomData[CUSTOM_DATA_SHARED_PARENT_SLOT] = line.bUsesSharedParent ? 1.0f : 0.0f;
    component.SetCustomPrimitiveDataFloatArray(0, MakeArrayView(mCustomData));
}

//////////////////////////////////////////////////////////////////////////
// Returns the targeting selection as a line key (None when nothing orbiting is selected)
SOLOrbitLineMath::FLineKey ASOLOrbitLineVisuals::GetTargetKey(bool& outHasOrbit) const
{
    const int32 selected = Targeting != nullptr ? Targeting->GetSelectedIndex() : INDEX_NONE;
    return SOLOrbitLineLogic::TargetKeyForCandidate(selected, mRegistryParents, outHasOrbit);
}

//////////////////////////////////////////////////////////////////////////
// Rebuilds up to ORBIT_LINE_MAX_REBUILDS_PER_FRAME lines whose shape drifted, round robin
void ASOLOrbitLineVisuals::RebuildDriftedLines(const double centuries)
{
    // Flag every registry line whose semi-major axis or eccentricity drifted (orientation drift is a shader
    // rotation; asteroid lines never drift: their elements have no secular rates)
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    for (int32 slot = 0; slot < mLines.Num(); ++slot)
    {
        const FLineState& line = mLines[slot];
        mNeedsRebuild[slot] = line.VertexCount > 0 && line.Key.Source == SOLOrbitLineMath::ELineSource::RegistryBody
            && SOLOrbitLineLogic::NeedsShapeRebuild(line.BuiltElements,
                registry.GetElements(line.Key.Index).AtCenturies(centuries), line.BodyRadiusM);
    }

    // Rebuild at most ORBIT_LINE_MAX_REBUILDS_PER_FRAME of them, round robin
    SOLOrbitLineLogic::SelectDriftRebuilds(mNeedsRebuild, mRebuildCursor, SOL::ORBIT_LINE_MAX_REBUILDS_PER_FRAME,
        mRebuildSlots);
    for (const int32 slot : mRebuildSlots)
    {
        FLineState& line = mLines[slot];
        const FSOLKeplerElements now = registry.GetElements(line.Key.Index).AtCenturies(centuries);
        mTotalVertexCount -= line.VertexCount;
        line.VertexCount = BuildLineMesh(now, line.BodyRadiusM, LineMeshes[slot]);
        line.BuiltElements = now;
        mTotalVertexCount += line.VertexCount;
        ++mRebuildCount;
        UE_LOG(LogSOL, Log, TEXT("OrbitLineVisuals %s: rebuilt the line of %s after shape drift (rebuild %d)"),
            *GetName(), *registry.GetName(line.Key.Index).ToString(), mRebuildCount);
    }
    mRebuildCursor = SOLOrbitLineLogic::NextRebuildCursor(mRebuildCursor, mLines.Num());
}

//////////////////////////////////////////////////////////////////////////
// Writes the occluder spheres (viewpoint-relative) and the scene-depth trust limit into the collection
void ASOLOrbitLineVisuals::UpdateOccluders(const FVector3d& viewpointM)
{
    // The camera's universe position (last frame's camera, which only affects which spheres are picked)
    FVector3d cameraM = viewpointM;
    const APlayerController* controller = GetWorld()->GetFirstPlayerController();
    if (controller != nullptr && controller->PlayerCameraManager != nullptr)
    {
        cameraM = AnchorSubsystem->RenderToUniverseM(controller->PlayerCameraManager->GetCameraLocation());
    }
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    SOLOrbitLineMath::SelectOccluders(cameraM, registry.GetPositionsM(), mRegistryRadiiM,
        SOL::ORBIT_LINE_MIN_OCCLUDER_ANGULAR_RADIUS_RAD, SOL::ORBIT_LINE_MAX_OCCLUDERS, mOccluderIndices);
    for (int32 index = 0; index < SOL::ORBIT_LINE_MAX_OCCLUDERS; ++index)
    {
        FLinearColor value(0.0f, 0.0f, 0.0f, 0.0f);
        if (mOccluderIndices.IsValidIndex(index))
        {
            const int32 body = mOccluderIndices[index];
            const FVector3d centerM = registry.GetPositionM(body) - viewpointM;
            value = FLinearColor(static_cast<float>(centerM.X), static_cast<float>(centerM.Y),
                static_cast<float>(centerM.Z), static_cast<float>(mRegistryRadiiM[body]));
        }
        ParameterCollectionInstance->SetVectorParameterValue(mOccluderParamNames[index], value);
    }

    // Scene depth hides a line only nearer than any log-compressed body can be (SDD 17 Amendment 3)
    const double trustLimitCm = SOLOrbitLineLogic::SceneDepthTrustLimitCm(viewpointM, registry.GetPositionsM(),
        mRegistryRadiiM, SOL::DEFAULT_MAX_RENDER_DISTANCE_CM);
    ParameterCollectionInstance->SetVectorParameterValue(mStyle2ParamName, FLinearColor(
        static_cast<float>(SOL::ORBIT_LINE_HIGHLIGHT_MIN_ALPHA), static_cast<float>(SOL::ORBIT_LINE_AA_MARGIN_PX),
        static_cast<float>(SOL::ORBIT_LINE_DEPTH_BIAS), static_cast<float>(trustLimitCm)));
}
