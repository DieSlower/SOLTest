/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "HudEvents/SOLHudBus.h"
#include "Npc/SOLNpcTunnelMath.h"
#include "SOLConstants.h"

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Engine/Texture2D.h"

#include "SOLNpcTunnelComponent.generated.h"

class UMaterialInstanceDynamic;
class UStaticMesh;
class UTexture2D;
class USOLBodyRegistrySubsystem;
class USOLNpcHullInstances;
struct FSOLNpcGroupTable;

/**
 * The NPC lane guidance tunnels (SDD 49 sections 4.2-4.9, issue #49): for every lane within 100 lane radii of the
 * viewpoint a tube of cyan rings, keep-right guide dots, wall chevrons and end rings, laid out by the continuous
 * "accordion" spacing and placed entirely on the GPU by M_SOLNpcTunnel (Shaders/Private/SOLNpcTunnel.ush) from a
 * small per-lane texture. It never ticks: ASOLNpcVisuals::UpdateLanes calls UpdateTunnels once per redraw. Owns a slot
 * mesh built once (one fixed-bounds ISM instance per lane in range, rewritten only when that set changes), the tunnel
 * material, the tunnel texture and its upload ring (whose fade is also read by the lane and point materials, Q5), the
 * per-lane ring phases, and publishes the wall labels on the HUD event bus. Per frame O(lanes), no allocation.
 */
UCLASS()
class SOLTEST_API USOLNpcTunnelComponent : public USceneComponent
{
    GENERATED_BODY()

public:

    // Never ticks
    USOLNpcTunnelComponent();

    // Creates the tunnel texture (zeros), builds the slot mesh and the instanced component, loads the material and
    // sets its constant parameters; the texture exists even when the material is missing (every fade then reads 0).
    // Returns false without the material
    bool InitializeTunnels();

    // Returns the tunnel texture the lane and point materials bind as NpcTunnelTex (null before InitializeTunnels)
    UTexture2D* GetTunnelTexture() const { return TunnelTexture; }

    // Per redraw: the shown rule, each lane's frame, fade and ring phase, the instances on a change, the texels, the
    // material's frame and the label snapshot. frameParams = (viewpoint render cm, max render cm); cameraForward and
    // the viewpoint in universe ecliptic axes
    void UpdateTunnels(const FSOLNpcLaneFrame& laneFrame, const FSOLNpcGroupTable& groups, const FVector3d& viewpointM,
        const FVector3d& cameraForward, bool bLanesShown, bool bMapOpen, const FLinearColor& frameParams,
        uint64 frameNumber);

    // Pushes the HUD glow scale to the material (ASOLNpcVisuals' glow feed calls this only on a change)
    void SetGlowScale(float glowScale);

    // Verification runs: the tunnels are shown or hidden as given, the player's settings ignored; bForceAllLanes sets
    // every lane's fade to 1 (the all-lanes budget case, D14)
    void SetVerificationOverride(bool bShown, bool bForceAllLanes);

    // Returns whether a tunnel drew last update
    bool AreTunnelsDrawn() const { return mAreTunnelsDrawn; }

    // Returns whether the tunnels were shown last update (the shown rule, before any fade; verification)
    bool AreTunnelsShown() const { return mAreTunnelsShown; }

    // Returns the lanes with an instance (the lanes with a fade above 0, index order; verification)
    TConstArrayView<int32> GetInstanceLanes() const { return mInstanceLanes; }

    // Returns the shown lanes' renderer feed of the last update (verification)
    TConstArrayView<FSOLNpcTunnelLaneData> GetShownLanes() const { return mShownLanes; }

    // Returns the texels uploaded last (four rows of NPC_GROUP_TEXTURE_WIDTH; verification)
    TConstArrayView<FVector4f> GetLastTexels() const { return mTexels[mUploadRing]; }

    // Returns the tunnel material's GlowScale as the material holds it; false without the material (verification)
    bool GetPushedGlowScale(float& outGlowScale) const;

    // Returns the slot mesh's vertex count (verification)
    int32 GetSlotMeshVertexCount() const { return mSlotMeshVertexCount; }

    // Returns the instanced component's instance count (verification)
    int32 GetInstanceCount() const;

    // Returns whether the instanced component is visible (verification)
    bool IsInstanceComponentVisible() const;

protected:

    // Clears the label snapshot when the component goes away
    virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

private:

    // Builds the slot mesh once (rings, end rings, dot and chevron quads; vertices at zero, the slot in the UVs)
    void BuildSlotMesh();

    // Rewrites the instances to one per lane in mInstanceLanes (custom data 0 = the lane); on a change only
    void WriteInstances();

    // Fills and publishes the label snapshot (one label per shown tunnel, strongest first, plus the dot rows)
    void PublishLabels(TConstArrayView<FSOLNpcLaneEntry> lanes, const FSOLNpcGroupTable& groups,
        const FVector3d& viewpointM, uint64 frameNumber);

    UPROPERTY(Transient)
    TObjectPtr<USOLNpcHullInstances> Instances;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMesh> SlotMesh;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> TunnelMaterial;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> TunnelTexture;

    UPROPERTY(Transient)
    TObjectPtr<USOLBodyRegistrySubsystem> BodyRegistry;

    static constexpr int32 LANE_CAPACITY = SOL::NPC_GROUP_TEXTURE_WIDTH;
    TArray<FVector4f> mTexels[3];                       // Ring of tunnel-texture upload buffers (render thread lag)
    FUpdateTextureRegion2D mRegions[3];                 // Upload regions, one per ring slot
    int32 mUploadRing = 0;
    TArray<FSOLNpcTunnelLaneData> mShownLanes;          // This update's lanes with a fade above 0 (reserved once)
    TArray<int32> mInstanceLanes;                       // Lanes with an instance (reserved once)
    TArray<FTransform> mIdentityTransforms;             // Scratch for WriteInstances (reserved once)
    FSOLNpcTunnelFoot mFeet[LANE_CAPACITY];             // This update's foot per lane
    double mFades[LANE_CAPACITY] = {};                  // This update's effective fade per lane
    double mDistanceRadii[LANE_CAPACITY] = {};          // This update's distance in lane radii per lane
    double mPhases[LANE_CAPACITY] = {};                 // Ring phase per lane (held while in range)
    double mLastFootArcs[LANE_CAPACITY] = {};           // Foot arc of the last update per lane
    bool mIsInRange[LANE_CAPACITY] = {};                // Fade above 0 last update per lane
    int32 mLabelSigns[LANE_CAPACITY] = {};              // Label side of the foot point per lane (0 = none yet)
    int32 mSlotMeshVertexCount = 0;
    float mGlowScale = SOL::Hologram::HUD_GLOW_DEFAULT_LEVEL;
    bool mAreTunnelsShown = false;                      // Last update's shown rule
    bool mAreTunnelsDrawn = false;                      // Last update's draw (instances visible)
    bool mWasUploading = false;                         // A tunnel showed last update (the zeros go once after)
    bool mHasLabels = false;                            // A label snapshot is published
    bool mIsOverride = false;                           // Verification: the player's settings are not read
    bool mIsOverrideShown = true;                       // ...and the tunnels are shown as this says
    bool mIsForceAllLanes = false;                      // ...with every lane's fade forced to 1
    FSOLHudLaneTunnelLabels mLabels;                    // Scratch label snapshot (fixed capacity)
};
