/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "HudEvents/SOLHudBus.h"
#include "Npc/SOLNpcTunnelMath.h"
#include "SOLConstants.h"
#include "Traffic/SOLLaneNetworkMath.h"

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

// The view inputs of one tunnel update (SDD 53 sections 5.4, 7.1-7.3; part 53d): the live focal in 1080p-reference px
// and the QualityPreset's threshold scale (every pixel class threshold, fade and band x the scale), each group column's
// open state (the open fade's target; an empty view reads every column as open, no lane network), the real seconds
// since the last update (the 3 s open fade), and whether every open fade jumps to its target (a time jump)
struct FSOLNpcTunnelView
{
    double FocalRefPx = SOL::NPC_TUNNEL_REFERENCE_FOCAL_PX;
    double QualityScale = 1.0;
    TConstArrayView<uint8> ColumnOpen;
    double RealDeltaS = 0.0;
    bool bSnapOpenFades = false;
};

/**
 * The NPC lane guidance tunnels (SDD 49 sections 4.2-4.9, issue #49; far field SDD 52, issue #52): for every drawable
 * lane, at every distance in the flight view, a tube of cyan rings, keep-right guide dots, wall chevrons and end rings
 * on a flat 12 tunnel-radii lattice, a window of it placed entirely on the GPU by M_SOLNpcTunnel
 * (Shaders/Private/SOLNpcTunnel.ush) from a small per-lane texture and faded by on-screen size and ring gap. It never
 * ticks: ASOLNpcVisuals::UpdateLanes calls UpdateTunnels once per redraw. Owns a slot mesh built once (one fixed-bounds
 * ISM instance per drawn lane, rewritten only when that set changes), the tunnel material, the tunnel texture and its
 * upload ring (row 1 .w carries each lane's label strength, the old 100 -> 50 lane radii band, also read by the lane
 * and point materials, Q5 / SDD 52 D6-D7), the per-lane ring phases, and publishes the wall labels (by label strength)
 * on the HUD event bus. Per frame O(lanes), no allocation. Since SDD 53 (part 53d) each lane has a pixel class (Off:
 * nothing built; Reduced: the rings-and-end-rings mesh; Full: the whole slot mesh, by its largest on-screen radius with
 * hysteresis, thresholds x the QualityPreset scale), both classes share a fixed pool of 24 instances (largest first,
 * the overflow logged and counted), and a lane's tunnel fades over 3 s real time at a close or reopen (row 3 .z).
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

    // Per redraw: the shown rule, each lane's open fade and pixel class (Off lanes build nothing), the drawn lanes'
    // frame, fade and ring phase, the 24-instance pool shared by Reduced and Full (overflow logged once per change and
    // counted in stat SOLNpcTunnel), the instances on a change, the texels, the material's frame and quality scale and
    // the label snapshot. frameParams = (viewpoint render cm, max render cm); cameraForward and the viewpoint in
    // universe ecliptic axes
    void UpdateTunnels(const FSOLNpcLaneFrame& laneFrame, const FSOLNpcGroupTable& groups, const FVector3d& viewpointM,
        const FVector3d& cameraForward, bool bLanesShown, bool bMapOpen, const FLinearColor& frameParams,
        uint64 frameNumber, const FSOLNpcTunnelView& view = FSOLNpcTunnelView());

    // Returns per group column whether its tunnel open fade is above 0 (the slot hold of a closing lane, SDD 53 4.5)
    TConstArrayView<uint8> GetOpenFadeVisibleColumns() const { return TConstArrayView<uint8>(mOpenFadeVisible); }

    // Returns a lane's pixel class of the last update (Off out of range; verification)
    ESOLTunnelClass GetTunnelClass(int32 lane) const;

    // Returns a lane's open fade of the last update (0 out of range; verification)
    double GetOpenFade(int32 lane) const;

    // Returns a lane's largest on-screen tunnel radius of the last update, reference px (0 out of range or not stepped)
    double GetMaxRadiusPx(int32 lane) const { return lane >= 0 && lane < LANE_CAPACITY ? mMaxRadiusPx[lane] : 0.0; }

    // Returns the focal the label band and pixel fades read last update (live focal over the quality scale)
    double GetFadeFocalRefPx() const { return mFadeFocalRefPx; }

    // Returns the QualityPreset threshold scale of the last update (verification)
    double GetQualityScale() const { return mQualityScale; }

    // Returns the drawn lanes left without an instance last update (the pool's overflow, a fixed-pool limitation)
    int32 GetPoolOverflow() const { return mPoolOverflow; }

    // Returns the Full and Reduced instance counts and the Reduced mesh's vertex count (verification)
    int32 GetFullInstanceCount() const { return mFullLanes.Num(); }
    int32 GetReducedInstanceCount() const { return mReducedLanes.Num(); }
    int32 GetReducedMeshVertexCount() const { return mReducedMeshVertexCount; }

    // Pushes the HUD glow scale to the material (ASOLNpcVisuals' glow feed calls this only on a change)
    void SetGlowScale(float glowScale);

    // Verification runs: the tunnels are shown or hidden as given, the player's settings ignored; bForceAllLanes sets
    // every lane's label strength to 1 (labels and lane-ship points on every lane: the all-lanes budget case, D14)
    void SetVerificationOverride(bool bShown, bool bForceAllLanes);

    // Returns whether a tunnel drew last update
    bool AreTunnelsDrawn() const { return mAreTunnelsDrawn; }

    // Returns whether the tunnels were shown last update (the shown rule, before any fade; verification)
    bool AreTunnelsShown() const { return mAreTunnelsShown; }

    // Returns the lanes with an instance (the drawn lanes, index order; verification)
    TConstArrayView<int32> GetInstanceLanes() const { return mInstanceLanes; }

    // Returns the shown lanes' renderer feed of the last update (verification)
    TConstArrayView<FSOLNpcTunnelLaneData> GetShownLanes() const { return mShownLanes; }

    // Returns the texels uploaded last (four rows of NPC_GROUP_TEXTURE_WIDTH; verification)
    TConstArrayView<FVector4f> GetLastTexels() const { return mTexels[mUploadRing]; }

    // Returns the tunnel material's GlowScale as the material holds it; false without the material (verification)
    bool GetPushedGlowScale(float& outGlowScale) const;

    // Returns the slot mesh's vertex count (verification)
    int32 GetSlotMeshVertexCount() const { return mSlotMeshVertexCount; }

    // Returns the instance count of both classes' instanced components (verification)
    int32 GetInstanceCount() const;

    // Returns whether an instanced component (either class) is visible (verification)
    bool IsInstanceComponentVisible() const;

protected:

    // Clears the label snapshot when the component goes away
    virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

private:

    // Builds the Full slot mesh once (rings, end rings, dot and chevron quads; vertices at zero, the slot in the UVs)
    // and the Reduced mesh from its ring and end-ring part (the same first vertices and indices)
    void BuildSlotMesh();

    // Creates one class's instanced component (never culled, hidden until a tunnel of that class draws)
    USOLNpcHullInstances* CreateInstances(const TCHAR* name, UStaticMesh* mesh);

    // Rewrites a component's instances to one per lane in lanes (custom data 0 = the lane); on a change only
    static void WriteInstances(USOLNpcHullInstances& instances, TConstArrayView<int32> lanes,
        TArray<FTransform>& scratchTransforms);

    // Steps every column's open fade toward its open state (a column taken by another lane starts at 0)
    void StepOpenFades(const FSOLNpcGroupTable& groups, const FSOLNpcTunnelView& view);

    // Pushes the QualityPreset scale to the material's Layout4.z when it changed
    void PushQualityScale(double qualityScale);

    // Fills and publishes the label snapshot (one label per shown tunnel, strongest first, plus the dot rows)
    void PublishLabels(TConstArrayView<FSOLNpcLaneEntry> lanes, const FSOLNpcGroupTable& groups,
        const FVector3d& viewpointM, uint64 frameNumber);

    UPROPERTY(Transient)
    TObjectPtr<USOLNpcHullInstances> Instances;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMesh> SlotMesh;

    UPROPERTY(Transient)
    TObjectPtr<USOLNpcHullInstances> ReducedInstances;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMesh> ReducedMesh;

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
    TArray<FSOLNpcTunnelLaneData> mShownLanes;          // This update's drawn lanes (reserved once)
    TArray<int32> mInstanceLanes;                       // Lanes with an instance, both classes (reserved once)
    TArray<int32> mFullLanes;                           // Lanes with a Full instance, index order (reserved once)
    TArray<int32> mReducedLanes;                        // Lanes with a Reduced instance, index order (reserved once)
    TArray<int32> mNewFullLanes;                        // This update's Full pool lanes (scratch, reserved once)
    TArray<int32> mNewReducedLanes;                     // This update's Reduced pool lanes (scratch, reserved once)
    TArray<int32> mPoolLanes;                           // SelectTunnelPool's ranking (scratch, reserved once)
    TArray<FTransform> mIdentityTransforms;             // Scratch for WriteInstances (reserved once)
    FSOLNpcTunnelFoot mFeet[LANE_CAPACITY];             // This update's foot per lane
    double mFades[LANE_CAPACITY] = {};                  // This update's label strength per lane (old band, D7)
    double mDrawFades[LANE_CAPACITY] = {};              // This update's draw fade per lane (TunnelDrawFade)
    double mDistanceRadii[LANE_CAPACITY] = {};          // This update's distance in lane radii per lane
    double mMaxRadiusPx[LANE_CAPACITY] = {};            // This update's largest on-screen tunnel radius per lane
    double mOpenFades[LANE_CAPACITY] = {};              // Open fade per column (3 s real, Q9; row 3 .z factor)
    uint8 mPoolDrawn[LANE_CAPACITY] = {};               // This update's built (Reduced or Full) lanes, the pool input
    uint8 mInPool[LANE_CAPACITY] = {};                  // Lanes with an instance (last update's: the pool hysteresis)
    uint8 mOpenFadeVisible[LANE_CAPACITY] = {};         // Open fade above 0 per column (the slot hold)
    int32 mFadeBodyA[LANE_CAPACITY] = {};               // End points the open fade belongs to (a new lane restarts)
    int32 mFadeBodyB[LANE_CAPACITY] = {};               // As mFadeBodyA, end B
    ESOLTunnelClass mClasses[LANE_CAPACITY] = {};       // Pixel class per lane (hysteresis state)
    int32 mLabelSigns[LANE_CAPACITY] = {};              // Label side of the foot point per lane (0 = none yet)
    int32 mLabelBodyA[LANE_CAPACITY] = {};              // End points of the label side (a reused column resets it)
    int32 mLabelBodyB[LANE_CAPACITY] = {};              // As mLabelBodyA, end B
    int32 mSlotMeshVertexCount = 0;
    int32 mReducedMeshVertexCount = 0;
    int32 mPoolOverflow = 0;                            // Drawn lanes without an instance last update (reported)
    double mPushedQualityScale = -1.0;                  // Layout4.z as last pushed (-1: not yet)
    double mFadeFocalRefPx = SOL::NPC_TUNNEL_REFERENCE_FOCAL_PX; // Last update's fade focal (verification)
    double mQualityScale = 1.0;                         // Last update's QualityPreset threshold scale (verification)
    bool mHasOpenFades = false;                         // The open fades were set once (the first update jumps)
    float mGlowScale = SOL::Hologram::HUD_GLOW_DEFAULT_LEVEL;
    bool mAreTunnelsShown = false;                      // Last update's shown rule
    bool mAreTunnelsDrawn = false;                      // Last update's draw (instances visible)
    bool mWasUploading = false;                         // A tunnel showed last update (the zeros go once after)
    bool mHasLabels = false;                            // A label snapshot is published
    bool mIsOverride = false;                           // Verification: the player's settings are not read
    bool mIsOverrideShown = true;                       // ...and the tunnels are shown as this says
    bool mIsForceAllLanes = false;                      // ...with every lane's label strength forced to 1
    FSOLHudLaneTunnelLabels mLabels;                    // Scratch label snapshot (fixed capacity)
};
