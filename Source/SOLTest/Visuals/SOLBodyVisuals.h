/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "SOLConstants.h"

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectKey.h"

#include "SOLBodyVisuals.generated.h"

class UDirectionalLightComponent;
class UMaterialInstanceDynamic;
class UPostProcessComponent;
class UPrimitiveComponent;
class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class UStaticMeshComponent;

/**
 * Presentation proxy for the body registry: one sphere mesh per body with a recognizable parametric material, and a
 * post-process with bloom and a manual exposure it moves itself from the bodies in view (SOLSceneExposure). Bodies
 * shade themselves: every frame each body's material receives the unit direction from that body toward the Sun, so
 * every phase is correct wherever the body is relative to the observer. A shadowless directional light, aimed from
 * the Sun toward the observer, is kept for lit objects near the observer (the ship in 1b); the unlit body material
 * ignores it. The ship and dropped targets sit on lighting channel 1: while a planet or ring shadow covers the
 * observer (SOLRingShadow::BodySunTransmittance, SDD 16), a second light lights them at the shadowed intensity. It does
 * not tick; it re-places everything, and updates the exposure, when the anchor subsystem broadcasts its per-frame
 * universe update.
 */
UCLASS()
class SOLTEST_API ASOLBodyVisuals : public AActor
{
    GENERATED_BODY()

public:

    // Creates the root, the sun light and the post-process component
    ASOLBodyVisuals();

    // Hides or shows one body's placeholder mesh (a dedicated visual such as ASOLGasGiantVisuals draws it instead);
    // returns false if the meshes are not created yet or the index is invalid. Exposure still counts the body
    bool SetBodyMeshHidden(int32 bodyIndex, bool bHide);

    // Returns true if a body's placeholder mesh exists and is currently hidden
    bool IsBodyMeshHidden(int32 bodyIndex) const;

    // Adds or removes a requester for the sun light acting as the atmosphere sun light (SDD 14 Amendment 3): it is on
    // while at least one requester (a gas giant showing its near volumetric cloud layer) holds it, so the engine
    // clouds are lit by it; off otherwise. Its sun disk is never drawn
    void SetSunDrivesAtmosphere(const UObject* requester, bool bDrives);

    // Returns true while the sun light is flagged as the atmosphere sun light
    bool IsSunDrivingAtmosphere() const;

    // Returns the fraction of sunlight every body's and ring's shadow leaves at the observer (1 before any update)
    double GetShipSunTransmittance() const { return mShipSunTransmittance; }

    // Returns the Sun light (lighting channel 0, plus channel 1 while the observer is lit)
    const UDirectionalLightComponent* GetSunLight() const { return SunLight; }

    // Returns the ship's sun light (lighting channel 1 only; shown while the observer is in a shadow)
    const UDirectionalLightComponent* GetShipSunLight() const { return ShipSunLight; }

protected:

    // Creates one mesh and material instance per body and subscribes to universe updates
    virtual void BeginPlay() override;

    // Unsubscribes from universe updates
    virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

private:

    // Places every body mesh, updates each body's Sun direction and re-aims the sun light for the current frame
    void HandleUniverseUpdated();

    // Moves the manual exposure toward the target set by the bodies in view (SDD 13 decision 2, Amendment 1)
    void UpdateExposure(const FVector3d& sunPositionM);

    // Shadows the ship's and targets' sunlight by every body and ring at the observer (SDD 16, 16d)
    void UpdateShipSunlight(const FVector3d& sunPositionM);

    /** Scene root at the render origin. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Visuals")
    TObjectPtr<USceneComponent> SceneRoot;

    /** Sunlight for lit objects near the observer, aimed from the Sun toward the observer every frame. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Visuals")
    TObjectPtr<UDirectionalLightComponent> SunLight;

    /** Shadowed sunlight for the ship and dropped targets (channel 1), shown only while the observer is shadowed. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Visuals")
    TObjectPtr<UDirectionalLightComponent> ShipSunLight;

    /** Unbound post-process: bloom and the manual exposure (bias 0 = the old fixed exposure) UpdateExposure moves. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Visuals")
    TObjectPtr<UPostProcessComponent> PostProcess;

    /** Sunlight illuminance in lux for the light and the body shading (tuned at the dark-sky exposure, EV100 0). */
    UPROPERTY(EditAnywhere, Category = "SOL|Visuals", meta = (ClampMin = "0.0"))
    float SunIlluminanceLux = SOL::SUN_ILLUMINANCE_LUX;

    /** One sphere per body, indexed like the body registry. */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> BodyMeshes;

    /** One material instance per body, indexed like the body registry. */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInstanceDynamic>> BodyMaterials;

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> AnchorSubsystem;

    UPROPERTY(Transient)
    TObjectPtr<USOLBodyRegistrySubsystem> BodyRegistrySubsystem;

    TArray<int32> mSunDirectionParamIndices; // Per body: cached SunDirection parameter index in its material instance
    FDelegateHandle mUniverseUpdatedHandle;  // Subscription to the anchor subsystem's per-frame update
    TArray<double> mAlbedoLuminances;        // Per body: mean albedo luminance (exposure estimate)
    TArray<double> mEmissiveLuminances;      // Per body: emissive luminance (exposure estimate)
    TArray<int32> mRingProfileIndices;       // Per body: SOL::RING_PROFILE_INDEX_* of its rings, or -1 (no rings)
    double mShipSunTransmittance = 1.0;      // Sunlight fraction left at the observer by every body and ring shadow
    bool mIsShipShadowed = false;            // True while the ship sun light is shown (Sun light on channel 0 only)
    double mExposureEv100 = SOL::EXPOSURE_MIN_EV100; // Smoothed exposure; bias = EXPOSURE_MIN_EV100 - this
    bool mExposureInitialized = false;       // False until the first update sets the exposure to its target
    int32 mSunIndex = INDEX_NONE;            // Registry index of the Sun (light source)
    TSet<FObjectKey> mSunAtmosphereRequesters; // Holders of SetSunDrivesAtmosphere(true); flag on while non-empty
};

// The ship sunlight's lighting channel (SDD 16, 16d), shared by every primitive it lights
namespace SOLShipSunlight
{
    // Puts a primitive on lighting channel 1 only (the ship's parts and the dropped targets), so the Sun light reaches
    // it only while the observer is lit and the ship sun light while it is shadowed. Safe before registration
    SOLTEST_API void PutOnShipLightingChannel(UPrimitiveComponent* primitive);
}

// Read access to the placeholder body appearances, for presentation that must match the meshes (map icons)
namespace SOLBodyAppearance
{
    // Writes a body's base color (the material's linear ColorA) and returns true, or returns false if it has none
    SOLTEST_API bool FindBaseColor(FName bodyName, FLinearColor& outColor);
}
