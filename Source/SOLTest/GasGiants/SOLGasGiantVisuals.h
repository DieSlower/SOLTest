/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "GasGiants/SOLGasGiantMath.h"
#include "SOLConstants.h"

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SOLGasGiantVisuals.generated.h"

class ASOLBodyVisuals;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkyAtmosphereComponent;
class UStaticMeshComponent;
class UVolumetricCloudComponent;
class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLSimClockSubsystem;

/**
 * Presentation proxy for one gas or ice giant's band shells (SDD 14, 14c): an opaque cloud deck (the shared
 * M_SOLGasGiantDeck, parameterized per planet) drawn in place of the body's placeholder mesh, and a translucent haze
 * shell just above it (M_SOLGasGiantHaze). Both are placed exactly like the body mesh (the anchor's render placement,
 * log-compressed far away) and oriented with the body, so they spin with it; on top of that the bands drift by
 * latitude and the storms ride their bands, as a function of a drift clock that follows sim time but advances no
 * faster than SOL::GAS_GIANT_BAND_MAX_TIME_WARP (SOLGasGiant band phases and storm longitudes, reduced in double and
 * handed to the materials as floats). One actor per giant, spawned deferred by
 * ASOLGameMode with PlanetName and BodyVisuals set before BeginPlay. It does not tick: it updates on the anchor's
 * per-frame universe update, and skips the parameter upload while the giant cannot be on screen (no shell rendered
 * recently, near layer hidden, outside the player camera's view cone).
 * Near the giant (14d, SDD 14 Amendment 3) it also owns the near volumetric cloud layer: an engine VolumetricCloud
 * centred by a zero-scattering SkyAtmosphere placed at the giant's render-space centre, faded in by
 * SOLGasGiant::NearLayerBlend (the haze shell fades out in step), created on first show and destroyed after
 * SOL::GAS_GIANT_NEAR_RELEASE_DELAY_SECONDS hidden (StepNearLayer). The renderer draws one atmosphere and one cloud
 * layer per scene; the giants are far enough apart that at most one near layer is ever shown.
 */
UCLASS()
class SOLTEST_API ASOLGasGiantVisuals : public AActor
{
    GENERATED_BODY()

public:

    // Creates the root and the two shell meshes
    ASOLGasGiantVisuals();

    // Returns the registry names of the bodies that get an ASOLGasGiantVisuals (the four giants)
    static TConstArrayView<const TCHAR*> GetGasGiantBodyNames();

    // Returns the opaque deck shell (for inspection and tests)
    UStaticMeshComponent* GetDeckMesh() const { return DeckMesh; }

    // Returns the translucent haze shell (for inspection and tests)
    UStaticMeshComponent* GetHazeMesh() const { return HazeMesh; }

    // Returns true while the near volumetric layer's components exist (for inspection and tests)
    bool IsNearLayerActive() const { return mNearLayer.bActive; }

    // Returns true while the near volumetric layer is shown (for inspection and tests)
    bool IsNearLayerShown() const { return mIsNearLayerShown; }

    // Returns the near layer's cloud component, or null while released (for inspection and tests)
    UVolumetricCloudComponent* GetNearClouds() const { return NearClouds; }

    /** Giant's registry name; the spawner must set this before BeginPlay. */
    UPROPERTY(EditAnywhere, Category = "SOL|GasGiants")
    FName PlanetName;

    /** Body visuals whose placeholder mesh for this giant is hidden once the deck is up; set by the spawner. */
    UPROPERTY(Transient)
    TObjectPtr<ASOLBodyVisuals> BodyVisuals;

protected:

    // Resolves the body, creates the material instances, generates the storms and subscribes to universe updates
    virtual void BeginPlay() override;

    // Unsubscribes from universe updates and shows the placeholder body mesh again
    virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

private:

    // Places both shells like the body mesh, and pushes this frame's band phases, storm positions and Sun direction
    void HandleUniverseUpdated();

    // Writes this frame's band/haze phases and storm longitudes (at the drift clock) into the material instances
    void UpdateAnimation(double driftSeconds);

    // Advances the near volumetric layer (blend from the observer distance, 20 s release) and places it; returns blend
    double UpdateNearLayer(const FVector3d& centerCm, const FQuat& rotation, double radiusM);

    // Creates and registers the centre-carrying atmosphere and the volumetric cloud layer (hidden until shown)
    void ActivateNearLayer(double radiusM);

    // Destroys the near layer's components and material, freeing their render resources
    void ReleaseNearLayer();

    // Shows or hides the near layer's components and flags the sun light as the atmosphere light accordingly
    void SetNearLayerShown(bool bShown);

    /** Scene root at the render origin. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Visuals")
    TObjectPtr<USceneComponent> SceneRoot;

    /** Opaque banded cloud deck at the planet radius. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Visuals")
    TObjectPtr<UStaticMeshComponent> DeckMesh;

    /** Translucent high haze at SOL::GAS_GIANT_HAZE_RADIUS_SCALE planet radii. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Visuals")
    TObjectPtr<UStaticMeshComponent> HazeMesh;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> DeckMaterial;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> HazeMaterial;

    /** Near layer (14d): zero-scattering atmosphere that only carries the planet centre for the cloud renderer. */
    UPROPERTY(Transient)
    TObjectPtr<USkyAtmosphereComponent> NearAtmosphere;

    /** Near layer (14d): engine volumetric cloud layer just above the deck, created on demand. */
    UPROPERTY(Transient)
    TObjectPtr<UVolumetricCloudComponent> NearClouds;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> CloudMaterial;

    /** Near cloud material, loaded once in BeginPlay; null leaves the near layer permanently inert. */
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> CloudMaterialAsset;

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> AnchorSubsystem;

    UPROPERTY(Transient)
    TObjectPtr<USOLBodyRegistrySubsystem> BodyRegistry;

    UPROPERTY(Transient)
    TObjectPtr<USOLSimClockSubsystem> SimClock;

    int32 mBodyIndex = INDEX_NONE;               // Registry index of this giant
    int32 mSunIndex = INDEX_NONE;                // Registry index of the Sun
    int32 mAppearanceIndex = INDEX_NONE;         // Row of the per-planet appearance table
    bool mIsBodyMeshHidden = false;              // Whether the placeholder body mesh has been hidden yet
    bool mHasAnimated = false;                   // Whether the materials have received their first animation update
    TArray<SOLGasGiant::FStorm> mStorms;         // Generated once in BeginPlay (primary storm first), <= MAX_STORMS
    int32 mBandParamIndices[UE_ARRAY_COUNT(SOL::GasGiantMaterialParams::BAND_PHASES)] = {};  // Deck parameter slots
    int32 mHazeParamIndices[UE_ARRAY_COUNT(SOL::GasGiantMaterialParams::HAZE_PHASES)] = {};  // Haze parameter slots
    int32 mStormParamIndices[UE_ARRAY_COUNT(SOL::GasGiantMaterialParams::STORMS)] = {};      // Deck storm slots
    int32 mDeckSunParamIndex = INDEX_NONE;       // Deck SunDirection slot
    int32 mHazeSunParamIndex = INDEX_NONE;       // Haze SunDirection slot
    int32 mHazeShapeParamIndex = INDEX_NONE;     // Haze ShapeParams slot (its .y opacity fades with the near layer)
    int32 mCloudBandParamIndices[UE_ARRAY_COUNT(SOL::GasGiantMaterialParams::BAND_PHASES)] = {}; // Cloud phase slots
    int32 mCloudCenterParamIndex = INDEX_NONE;   // Cloud PlanetCenter slot
    int32 mCloudAxisParamIndices[3] = {};        // Cloud BodyAxisX/Y/Z slots
    int32 mCloudBlendParamIndex = INDEX_NONE;    // Cloud NearBlend slot
    int32 mDeckRingSunParamIndex = INDEX_NONE;   // Deck RingShadowSun slot
    int32 mHazeRingSunParamIndex = INDEX_NONE;   // Haze RingShadowSun slot
    int32 mCloudRingSunParamIndex = INDEX_NONE;  // Cloud RingShadowSun slot
    FLinearColor mRingShadowSun = FLinearColor::Transparent; // This frame's RingShadowSun (local Sun direction, alpha)
    FLinearColor mRingShadowParams = FLinearColor(-1.0f, 0.0f, 0.0f, 0.0f); // RingShadowParams (profile, radius km)
    SOLGasGiant::FNearLayerState mNearLayer;     // Near layer residency (StepNearLayer)
    bool mIsNearLayerShown = false;              // Whether the near layer's components are visible
    float mHazeOpacity = 0.0f;                   // Haze opacity currently in the haze ShapeParams
    double mLastSimSeconds = 0.0;                // Sim time at the previous universe update
    double mDriftSeconds = 0.0;                  // Drift clock: sim time advanced at most GAS_GIANT_BAND_MAX_TIME_WARP
    float mPhaseScratch[SOL::GAS_GIANT_BAND_SAMPLE_COUNT] = {}; // Per-frame band phases, no allocation
    FDelegateHandle mUniverseUpdatedHandle;      // Subscription to the anchor's per-frame update
};
