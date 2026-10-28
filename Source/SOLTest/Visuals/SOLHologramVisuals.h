/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SOLHologramVisuals.generated.h"

class APlayerController;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPostProcessComponent;
class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLMapModeSubsystem;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Map hologram look (SDD 20 section 2.4, plan step 6), layers (a) and (b): while the map is open and sol.Hologram is
 * on, every body is drawn as an unlit additive hologram proxy sphere (M_SOLHologramBody: Fresnel rim, contour lines,
 * time noise and 1 px scanlines; the Sun in Secondary, planets and moons in Primary) and every ringed planet's rings as
 * a hologram disc (M_SOLHologramRing), and a map-only post-process (M_SOLHologramPost after tonemapping: tint,
 * scanlines, flicker and a small row jitter, scaled by sol.HologramPostStrength) runs over the scene with a fixed
 * exposure.
 *
 * The real bodies, rings and gas giants are not touched: their actors are added to the player controller's HiddenActors
 * list while the look shows (the renderer skips them for that player's view only) and removed when it stops, so their
 * own visibility logic (gas giants hiding the placeholder mesh, ring layers) keeps running and nothing stays hidden
 * after the map closes. Ring shadows and gas-giant detail are therefore not drawn in the map. The grid and stems
 * overlay (layer c) is drawn by the HUD (FSOLMapHologramOverlay) while IsHologramEnabled().
 *
 * It does not tick: it follows the anchor subsystem's per-frame universe update, switches on the transitions of
 * SOLHologramMath::NextTransition and, while shown, places the proxies exactly as ASOLBodyVisuals places the bodies.
 */
UCLASS()
class SOLTEST_API ASOLHologramVisuals : public AActor
{
    GENERATED_BODY()

public:

    // Creates the root and the (disabled) map post-process
    ASOLHologramVisuals();

    // Returns true while the hologram look shows (map open and sol.Hologram on)
    bool IsShown() const { return mIsShown; }

    // Returns the number of proxy components (body spheres plus ring discs) currently visible (verification)
    int32 CountVisibleProxies() const;

    // Returns the number of proxy components created (verification)
    int32 GetProxyCount() const { return BodyProxies.Num() + RingProxies.Num(); }

    // Returns the number of real actors this look currently keeps hidden (verification)
    int32 GetHiddenActorCount() const { return mHiddenActors.Num(); }

    // Returns true while the map post-process is enabled (verification)
    bool IsPostProcessEnabled() const;

protected:

    // Creates the proxies and the post-process material, then follows the universe updates
    virtual void BeginPlay() override;

    // Restores the real visuals if shown and unsubscribes
    virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

private:

    // Switches on map/cvar transitions and, while shown, places the proxies and updates the post-process strength
    void HandleUniverseUpdated();

    // Hides the real bodies, rings and gas giants for the player's view and shows the proxies and the post-process;
    // does nothing (retried next frame) while there is no player controller
    void Show();

    // Gives the real visuals back and hides the proxies and the post-process
    void Hide();

    // Places every body proxy and ring disc for this frame, exactly as ASOLBodyVisuals places the bodies
    void PlaceProxies();

    // Creates one unlit proxy component (hidden) with its own material instance
    UStaticMeshComponent* CreateProxy(const FString& name, UStaticMesh* mesh, UMaterialInterface* material,
        const FLinearColor& color, double intensity);

    /** Scene root at the render origin. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Visuals")
    TObjectPtr<USceneComponent> SceneRoot;

    /** Map-only post-process (tint, scanlines, flicker, jitter; fixed exposure); enabled only while shown. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Visuals")
    TObjectPtr<UPostProcessComponent> PostProcess;

    /** One hologram sphere per body, indexed like the body registry. */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> BodyProxies;

    /** One hologram ring disc per ringed planet (SOLPlanetRing::RealRings order). */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> RingProxies;

    /** Instance of M_SOLHologramPost blended by PostProcess (tint and strength set from C++). */
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> PostMaterial;

    /** Source of the per-frame universe update and the body render placement. */
    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> AnchorSubsystem;

    /** Body names, parents, radii and orientations. */
    UPROPERTY(Transient)
    TObjectPtr<USOLBodyRegistrySubsystem> BodyRegistrySubsystem;

    /** Whether the map is open. */
    UPROPERTY(Transient)
    TObjectPtr<USOLMapModeSubsystem> MapMode;

    TArray<int32> mRingPlanetIndices;               // Per ring disc: registry index of its planet
    TArray<double> mRingOuterRadiiM;                // Per ring disc: outer radius (m)
    TArray<TWeakObjectPtr<AActor>> mHiddenActors;   // Real actors added to the controller's HiddenActors
    TWeakObjectPtr<APlayerController> mHidingController; // Controller whose HiddenActors holds them
    FDelegateHandle mUniverseUpdatedHandle;         // Subscription to the anchor subsystem's per-frame update
    float mPostStrength = -1.0f;                    // Strength last written to the post material
    bool mIsShown = false;                          // The look shows
};

// The map hologram look's console switches (cvars sol.Hologram and sol.HologramPostStrength)
namespace SOLHologramSwitch
{
    // Returns true while sol.Hologram is on (default 1): proxies, post-process, grid and stems in the map
    SOLTEST_API bool IsHologramEnabled();

    // Returns sol.HologramPostStrength clamped (SOLHologramMath::ClampPostStrength)
    SOLTEST_API double GetPostStrength();
}
