/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "Components.h"
#include "GameFramework/Actor.h"

#include "MinorBodies/SOLAsteroidBelt.h"
#include "OrbitLines/SOLOrbitLineMath.h"
#include "SOLConstants.h"
#include "Universe/SOLKepler.h"

#include "SOLOrbitLineVisuals.generated.h"

class UMaterialInterface;
class UMaterialParameterCollection;
class UMaterialParameterCollectionInstance;
class UStaticMesh;
class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLMenuSubsystem;
class USOLOrbitLineComponent;
class USOLSettingsSubsystem;
class USOLSimClockSubsystem;
class USOLTargetingSubsystem;

/**
 * World-space orbit lines (SDD 17): one runtime-built ribbon mesh per permanent line (every registry body with a
 * parent and the largest named asteroids) plus one transient line for a target without a permanent one. Geometry is
 * built once; secular drift of the orbit's orientation is applied as a rotation in the vertex shader, and only shape
 * drift (semi-major axis, eccentricity) rebuilds a mesh. Each frame writes per-line custom primitive data (parent
 * offset, rotation, highlight) and the shared parameter collection.
 */
UCLASS()
class SOLTEST_API ASOLOrbitLineVisuals : public AActor
{
    GENERATED_BODY()

public:

    // Creates the root; no tick (updates run on USOLAnchorSubsystem::OnUniverseUpdated)
    ASOLOrbitLineVisuals();

    // Shows or hides every orbit line, the targeted one included
    void SetLinesEnabled(bool bEnabled);

    // Flips the lines (the O key): through the settings subsystem when there is one, so the choice is saved at the
    // next write point, else locally
    void ToggleLines();

    // Verification runs (-SOLSmokeHud, -SOLSmokeMap): detaches the lines from the settings subsystem, so the smoke's
    // toggles never read or change the player's saved choice, and starts them on
    void BeginVerificationOverride();

    // Returns the number of line rebuilds since BeginPlay (verification)
    int32 GetRebuildCount() const { return mRebuildCount; }

    // Returns true while the lines are enabled (the setting; they are still hidden behind the main menu)
    bool AreLinesEnabled() const { return mIsEnabled; }

    // Returns the number of permanent lines
    int32 GetPermanentLineCount() const { return mLines.Num(); }

    // Returns the number of line components currently visible (verification)
    int32 CountVisibleLines() const;

    // Returns the total ribbon vertex count of the permanent lines
    int32 GetTotalVertexCount() const { return mTotalVertexCount; }

    // Returns the permanent line highlighted last frame, or INDEX_NONE
    int32 GetHighlightedSlot() const { return mHighlightedSlot; }

    // Returns true while the transient target line is shown
    bool IsTransientLineShown() const { return mIsTransientShown; }

protected:

    // Resolves subsystems and assets, builds every permanent line and subscribes to universe updates
    virtual void BeginPlay() override;

    // Unsubscribes from universe updates
    virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

private:

    // One line's source, the elements its mesh was built from and its last applied state
    struct FLineState
    {
        SOLOrbitLineMath::FLineKey Key;
        int32 ParentIndex = INDEX_NONE;          // Registry index of the body the line is relative to
        double BodyRadiusM = 0.0;                // Radius of the orbiting body (segment count and drift tolerance)
        FSOLKeplerElements BuiltElements;        // Elements the current mesh was sampled from
        int32 VertexCount = 0;                   // Ribbon vertices in the current mesh (0 = nothing drawn)
        bool bUsesSharedParent = false;          // Sun-parented asteroid line: parent offset from the collection
        bool bIsShown = true;                    // Component visibility last applied
        bool bWasHighlighted = false;            // Highlight last written (asteroid data is written only on change)
        bool bHasWrittenData = false;            // Custom data written at least once
    };

    // Resolves a key's current elements, parent and body radius; false if it has no drawable orbit
    bool ResolveLineSource(const SOLOrbitLineMath::FLineKey& key, FSOLKeplerElements& outElements,
        int32& outParentIndex, double& outRadiusM) const;

    // Samples the elements and (re)builds the mesh; returns the vertex count (0 if the orbit is not drawable)
    int32 BuildLineMesh(const FSOLKeplerElements& elements, double bodyRadiusM, UStaticMesh* mesh);

    // Creates a line component with its own mesh, registered and attached to the root
    USOLOrbitLineComponent* CreateLineComponent(UStaticMesh*& outMesh);

    // Writes the per-frame line data, the highlight, the transient line and the occluders
    void HandleUniverseUpdated();

    // Writes one line's custom primitive data (parent offset, rotation from its built orientation, highlight)
    void WriteLineData(USOLOrbitLineComponent& component, const FLineState& line, const FVector3d& viewpointM,
        const FQuat4d& rotation, bool bHighlighted);

    // Returns the targeting selection as a line key (None when nothing orbiting is selected)
    SOLOrbitLineMath::FLineKey GetTargetKey(bool& outHasOrbit) const;

    // Rebuilds up to ORBIT_LINE_MAX_REBUILDS_PER_FRAME lines whose shape drifted, round robin
    void RebuildDriftedLines(double centuries);

    // Writes the occluder spheres (viewpoint-relative) and the scene-depth trust limit into the collection
    void UpdateOccluders(const FVector3d& viewpointM);

    // Returns whether the lines should be drawn now (setting on, main menu not showing, sol.OrbitLines on)
    bool ShouldDrawLines() const;

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> AnchorSubsystem;

    UPROPERTY(Transient)
    TObjectPtr<USOLBodyRegistrySubsystem> BodyRegistry;

    UPROPERTY(Transient)
    TObjectPtr<USOLSimClockSubsystem> SimClock;

    UPROPERTY(Transient)
    TObjectPtr<USOLTargetingSubsystem> Targeting;

    UPROPERTY(Transient)
    TObjectPtr<USOLMenuSubsystem> Menu;

    UPROPERTY(Transient)
    TObjectPtr<USOLSettingsSubsystem> Settings;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> LineMaterial;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialParameterCollection> ParameterCollection;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialParameterCollectionInstance> ParameterCollectionInstance;

    UPROPERTY(Transient)
    TArray<TObjectPtr<USOLOrbitLineComponent>> LineComponents;     // One per permanent line, same order as mLines

    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMesh>> LineMeshes;                    // Each permanent line's ribbon mesh

    UPROPERTY(Transient)
    TObjectPtr<USOLOrbitLineComponent> TransientComponent;        // The target line without a permanent line

    UPROPERTY(Transient)
    TObjectPtr<UStaticMesh> TransientMesh;

    TArray<FLineState> mLines;                                     // Permanent lines (SOLOrbitLineMath order)
    TArray<SOLOrbitLineMath::FLineKey> mLineKeys;                  // Same keys, for ResolveTargetLine
    FLineState mTransientLine;                                     // Key INDEX_NONE until a transient line is built
    TArray<FSOLAsteroidDef> mAsteroids;                            // Real named asteroids (MinorBody line indices)
    TArray<double> mRegistryRadiiM;                                // Registry radii, for SelectOccluders
    TArray<int32> mRegistryParents;                                // Registry parents, for the target mapping
    TArray<bool> mNeedsRebuild;                                    // Per-line drift flags (scratch, sized once)
    TArray<int32> mRebuildSlots;                                   // Lines rebuilt this frame (scratch)
    TArray<int32> mOccluderIndices;                                // Scratch, reserved once
    TArray<FVector3d> mScratchPointsM;                             // Sampling scratch (rebuilds only)
    TArray<FVector3d> mScratchTangents;
    TArray<double> mScratchArcLengthsM;
    TArray<FStaticMeshBuildVertex> mScratchVertices;               // Ribbon-building scratch (rebuilds only)
    TArray<uint32> mScratchIndices;
    float mCustomData[SOL::ORBIT_LINE_CUSTOM_DATA_TOTAL_FLOATS] = {}; // Per-line custom primitive data scratch
    FName mFrameParamName;                                         // Cached collection parameter names
    FName mStyle2ParamName;
    FName mSharedParentHighParamName;
    FName mSharedParentLowParamName;
    FName mOccluderParamNames[SOL::ORBIT_LINE_MAX_OCCLUDERS];
    FDelegateHandle mUniverseUpdatedHandle;
    int32 mSunIndex = INDEX_NONE;
    int32 mRebuildCursor = 0;                                      // Round-robin start of the next drift scan
    int32 mTotalVertexCount = 0;
    int32 mRebuildCount = 0;
    double mRibbonSeconds = 0.0;                                   // Load time spent building ribbon vertices (log)
    double mRenderDataSeconds = 0.0;                               // Load time spent creating render data (log)
    int32 mHighlightedSlot = INDEX_NONE;
    bool mIsEnabled = true;                                        // The setting (or the local toggle without one)
    bool mIsDrawn = true;                                          // Actor visibility last applied (ShouldDrawLines)
    bool mIsTransientShown = false;
    bool mIsVerificationOverride = false;                          // Settings ignored (smoke runs)
};
