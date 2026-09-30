/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "SOLConstants.h"

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SOLBodyVisuals.generated.h"

class UDirectionalLightComponent;
class UMaterialInstanceDynamic;
class UPostProcessComponent;
class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class UStaticMeshComponent;

/**
 * Presentation proxy for the body registry: one sphere mesh per body with a recognizable parametric material, and a
 * fixed-exposure post-process with bloom. Bodies shade themselves: every frame each body's material receives the unit
 * direction from that body toward the Sun, so every phase is correct wherever the body is relative to the observer. A
 * shadowless directional light, aimed from the Sun toward the observer, is kept for lit objects near the observer (the
 * ship in 1b); the unlit body material ignores it. It does not tick; it re-places everything when the anchor subsystem
 * broadcasts its per-frame universe update.
 */
UCLASS()
class SOLTEST_API ASOLBodyVisuals : public AActor
{
    GENERATED_BODY()

public:

    // Creates the root, the sun light and the post-process component
    ASOLBodyVisuals();

protected:

    // Creates one mesh and material instance per body and subscribes to universe updates
    virtual void BeginPlay() override;

    // Unsubscribes from universe updates
    virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

private:

    // Places every body mesh, updates each body's Sun direction and re-aims the sun light for the current frame
    void HandleUniverseUpdated();

    /** Scene root at the render origin. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Visuals")
    TObjectPtr<USceneComponent> SceneRoot;

    /** Sunlight for lit objects near the observer, aimed from the Sun toward the observer every frame. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Visuals")
    TObjectPtr<UDirectionalLightComponent> SunLight;

    /** Unbound post-process: fixed exposure and bloom. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Visuals")
    TObjectPtr<UPostProcessComponent> PostProcess;

    /** Sunlight illuminance in lux for the light and the body shading (tuned together with the fixed exposure). */
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
    int32 mSunIndex = INDEX_NONE;            // Registry index of the Sun (light source)
};

// Read access to the placeholder body appearances, for presentation that must match the meshes (map icons)
namespace SOLBodyAppearance
{
    // Writes a body's base color (the material's linear ColorA) and returns true, or returns false if it has none
    SOLTEST_API bool FindBaseColor(FName bodyName, FLinearColor& outColor);
}
