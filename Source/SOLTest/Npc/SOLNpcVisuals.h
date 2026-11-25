/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Npc/SOLNpcLaneMath.h"
#include "Npc/SOLNpcTierProcessor.h"
#include "UI/SOLHudShapeMath.h"
#include "Visuals/SOLThrusterMath.h"

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"

#include "SOLNpcVisuals.generated.h"

class ASOLOrbitLineVisuals;
class ASOLThrusterRibbons;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture2D;
class USOLAnchorSubsystem;
class USOLNpcSubsystem;
class USOLNpcTunnelComponent;
class USOLOrbitLineComponent;
class USOLSettingsSubsystem;

/**
 * Presentation of the NPC ships (SDD 11 section 3.4, Part 10c). It does not tick; it redraws on USOLNpcSubsystem's
 * OnNpcsUpdated.
 *
 * - Far: every Far ship is a GPU point. The rails are uploaded once per population into a float texture (two texels
 *   per ship, FSOLNpcRail), re-based to a recent epoch when the rail time has run far from the last one (so the
 *   shader's float time and phases stay small, SDD 11 Amendment 7), and a block mesh of NPC_POINT_BLOCK_SHIPS
 *   triangles is drawn once per block (one ISM instance each, the block's first ship index in its custom data).
 *   M_SOLNpcPoints's vertex shader evaluates each rail from the group points (a tiny per-frame texture, relative to the
 *   viewpoint), applies the far-depth compression of SOLRender::ComputePlacement, hides ships inside the Mid radius
 *   (drawn from the CPU instead), keeps a minimum pixel size and fades brightness with distance. Per frame only the
 *   group texture and a few parameters change, so 1M ships cost no per-frame CPU work or upload.
 * - CPU points: Mid and Full ships beyond the instanced radius but inside the Mid radius (beyond it the far layer
 *   already draws their rail point), positions written each frame into a float texture and drawn by the same material
 *   in CPU mode.
 * - Instanced: one ISM per faction placeholder hull, with per-instance previous transforms (motion vectors, issue
 *   #15) and a minimum on-screen size. Only the instances used this frame or last are updated; the instance count
 *   shrinks again when it stays far above the use.
 * - Mesh: a small pool of static mesh components for the closest ships; a slot that changes ship resets its scene
 *   velocity, so it has no motion vector from the previous owner.
 * - Plumes (SDD 51, part 51c): both hull layers draw the thruster hull meshes, whose plume, glow and burst sections
 *   read per-ship floats (SOL::ThrusterMaterialParams: c, b, the true plume length; bursts zero until 51g) from the
 *   instance custom data (ISMs) or the custom primitive data (pool), written in the placement pass from each ship's
 *   ThrustLocalG; only instances with a plume this frame or last are rewritten. Dot-drawn ships get none (Q27); the
 *   material fades and collapses a section by its true pixel size. sol.NpcPlumes 0 draws the plain shapes (A/B).
 * - Lane lines (SDD 38): each lane with ships is a pair of flowing dashed hologram lines, one world-space ribbon mesh
 *   built once per population (LaneLines, M_SOLNpcLanes) and placed on the GPU from the group texture, whose free .w
 *   channels carry the lane radius and traffic weight (ship counts from a histogram folded into the rail fill). The
 *   far points of lane ships cross-fade the other way (NpcParams4). Shown while the Lanes overlay row is (read from
 *   USOLSettingsSubsystem every frame) and sol.NpcLanes is on; the lines glow with the live HUD glow and their dashes
 *   flow on a real-time clock that ignores time warp and never stops (the pause menu included). The lanes share the
 *   orbit lines' occluders, which ASOLOrbitLineVisuals writes while lanes are drawn (SetOrbitLineVisuals).
 */
UCLASS()
class SOLTEST_API ASOLNpcVisuals : public AActor
{
    GENERATED_BODY()

public:

    // Creates the root and the instanced components
    ASOLNpcVisuals();

    // Returns this frame's lane frame (SDD 38 section 4.2; the extension point for issue #49)
    const FSOLNpcLaneFrame& GetLaneFrame() const { return mLaneFrame; }

    // Gives the lanes the orbit-line actor whose occluders they share (SDD 38 D24; ASOLGameMode, at spawn)
    void SetOrbitLineVisuals(ASOLOrbitLineVisuals* orbitLines);

    // Gives the placement pass the thruster ribbons it feeds the hull-drawn ships to (SDD 51 R7; ASOLGameMode, at
    // spawn)
    void SetThrusterRibbons(ASOLThrusterRibbons* ribbons) { ThrusterRibbons = ribbons; }

    // Verification runs (-SOLSmokeNpcLanes): detaches the lanes from the Lanes overlay row, so the player's settings
    // are neither read nor changed for them, and shows or hides them as given (may be called again to switch)
    void SetLaneVerificationOverride(bool bShown);

    // Verification runs (-SOLSmokeNpcTunnel, -SOLSmokeNpcLanes): shows or hides the lane tunnels as given without the
    // player's settings, optionally with every lane's fade forced to 1 (SDD 49 section 4.2)
    void SetTunnelVerificationOverride(bool bShown, bool bForceAllLanes);

    // Returns the lane tunnel component (verification)
    const USOLNpcTunnelComponent* GetTunnels() const { return Tunnels; }

    // Returns whether the lanes were shown last frame (the row or override, and sol.NpcLanes; verification)
    bool AreLanesShown() const { return mAreLanesShown; }

    // Returns whether the lane ribbon component is visible (verification)
    bool IsLaneComponentVisible() const;

    // Returns the lane ribbon's vertex count built for the current population (verification)
    int32 GetLaneMeshVertexCount() const { return mLaneMeshVertexCount; }

    // Returns the dash flow phase of the lane clock (verification)
    double GetLaneFlowPhase() const { return SOLNpcLane::LaneDashFlowPhase(mLaneFlowClockS); }

    // Returns the lane material's GlowScale and the far-point material's NpcParams4 as the materials hold them; false
    // without the materials (verification)
    bool GetPushedLaneParams(float& outGlowScale, FLinearColor& outPointLaneParams) const;

    // Returns the lane material's NpcLaneStyle2 (reference height, emissive, coarsest dash level) as the material
    // holds it; false without the material (verification)
    bool GetPushedLaneStyle2(FLinearColor& outStyle2) const;

    // Returns the group texels uploaded last frame (NPC_GROUP_TEXTURE_ROWS rows of NPC_GROUP_TEXTURE_WIDTH; verification)
    TConstArrayView<FVector4f> GetLastGroupTexels() const { return mGroupTexels[mUploadRing]; }

    // Returns the rail time the GPU copy of the rails is based at (the point shader's time is rail time minus this;
    // verification)
    double GetGpuRailEpochS() const { return mGpuRailEpochS; }

    // Reads the plume floats the component drawing a CPU ship (index into USOLNpcSubsystem::GetCpuShips) holds since
    // the last redraw: its hull instance's custom data or its pool mesh's custom primitive data (SDD 51 section 4.10);
    // false for a ship not drawn with a hull (verification, -SOLSmokeThruster)
    bool GetPushedPlume(int32 cpuShipIndex, FSOLPlumeInstance& outPlume) const;

    // Returns the 1080p-reference focal length the last redraw judged the plumes' pixel size with (verification)
    double GetPlumeFocalRefPx() const { return mPlumeFocalRefPx; }

protected:

    // Loads the assets, builds the block mesh and subscribes to NPC updates
    virtual void BeginPlay() override;

    // Unsubscribes from NPC updates
    virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

private:

    // Redraws every tier for this frame; bound to OnNpcsUpdated
    void Redraw();

    // Uploads the population's rails, re-based to the current rail time, and sizes the far blocks (once per population)
    void UploadPopulation();

    // Re-uploads the rails re-based to the current rail time, in place (rail time far from the GPU epoch)
    void ReepochRails();

    // Fills an upload buffer with the population's re-based rails (two texels per ship) and the lane histogram if asked
    void FillRailTexels(FVector4f* texels, int32 texelCount, double epochRailTimeS, TArray<int32>* outLaneShipCounts);

    // Uploads only the last block move's rails (SDD 53 4.5, 53c2b): its ship ranges' texels at the current GPU epoch,
    // packed into one source buffer and one region update (PackRailUploadRegions)
    void UploadMovedRails();

    // Takes the lane ships per column after a block move or re-share; rebuilds the ribbon mesh when a lane's drawn
    // state flips
    void ApplyLaneShipCounts();

    // Loads the lane material and sets its constant parameters (SDD 38 section 4.8); false if it is missing
    bool SetUpLanes();

    // Builds the lane ribbon mesh for the current population's lane ship counts (once per population)
    void BuildLaneMesh();

    // Per frame: the lanes shown, the far points' crossfade, the lane component's visibility, the flow clock, the
    // tunnels (SDD 49), the shared occluders' flag, the glow and the lane material's frame parameters
    void UpdateLanes(const FLinearColor& frameParams);

    // Updates one faction's hull instances: grows or shrinks the count and writes only the used range
    void UploadHulls(int32 faction);

    // Writes this frame's group points (relative to the viewpoint) and the point parameters
    void UpdateFarPoints();

    // Places the CPU ships: mesh pool, instanced hulls and CPU points
    void UpdateCpuShips();

    // Builds the point block mesh (NPC_POINT_BLOCK_SHIPS triangles; vertex z = ship index in the block)
    UStaticMesh* BuildPointBlockMesh();

    // Creates a float RGBA texture of the given size (nearest filtering, linear)
    UTexture2D* CreateFloatTexture(int32 width, int32 height) const;

    // Sizes a points ISM to blockCount instances, the custom data of each being its first ship index
    static void SizePointBlocks(UInstancedStaticMeshComponent* component, int32 blockCount, int32& inOutCurrent);

    // Returns the world size of one pixel at a render distance (cm), from the player camera and the viewport size
    double PixelWorldCm(double distanceCm) const;

    // Writes the player camera's horizontal FOV (degrees) and the viewport size (px), with reference fallbacks
    void ViewCamera(double& outFovDeg, FVector2D& outViewportPx) const;

    // Switches the hull ISMs and the mesh pool between the thruster hull meshes (plumes drawn) and the plain engine
    // shapes (sol.NpcPlumes 0, the A/B baseline); a switch back on rewrites every instance's plume floats
    void ApplyPlumeMeshes(bool bPlumes);

    // Writes the plume floats of the hull instances this frame or last frame had a plume on (the union of both
    // nonzero ranges; every instance outside it already holds zeros)
    void UploadHullPlumes(int32 faction, int32 range);

    UPROPERTY(VisibleAnywhere, Category = "SOL|Npc")
    TObjectPtr<UInstancedStaticMeshComponent> FarPoints;

    UPROPERTY(VisibleAnywhere, Category = "SOL|Npc")
    TObjectPtr<UInstancedStaticMeshComponent> CpuPoints;

    UPROPERTY(VisibleAnywhere, Category = "SOL|Npc")
    TObjectPtr<UInstancedStaticMeshComponent> HullsFaction0;

    UPROPERTY(VisibleAnywhere, Category = "SOL|Npc")
    TObjectPtr<UInstancedStaticMeshComponent> HullsFaction1;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> MeshPool;

    UPROPERTY(VisibleAnywhere, Category = "SOL|Npc")
    TObjectPtr<USOLOrbitLineComponent> LaneLines;

    UPROPERTY(VisibleAnywhere, Category = "SOL|Npc")
    TObjectPtr<USOLNpcTunnelComponent> Tunnels;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMesh> PlumeHullMeshes[2];       // Per faction: the hull plus plume, glow and burst sections

    UPROPERTY(Transient)
    TObjectPtr<UStaticMesh> PlainHullMeshes[2];       // Per faction: the engine shape alone (sol.NpcPlumes 0)

    UPROPERTY(Transient)
    TObjectPtr<UStaticMesh> PointBlockMesh;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMesh> LaneMesh;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> LaneMaterial;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> FarMaterial;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> CpuPointMaterial;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> RailTexture;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> GroupTexture;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> CpuTexture;

    UPROPERTY(Transient)
    TObjectPtr<USOLNpcSubsystem> Npcs;

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> Anchor;

    UPROPERTY(Transient)
    TObjectPtr<USOLSettingsSubsystem> Settings;

    UPROPERTY(Transient)
    TObjectPtr<ASOLOrbitLineVisuals> OrbitLines;

    UPROPERTY(Transient)
    TObjectPtr<ASOLThrusterRibbons> ThrusterRibbons;  // Fed from the placement pass (SDD 51 R7)

    FDelegateHandle mNpcsUpdatedHandle;
    uint32 mUploadedRevision = MAX_uint32;            // Population revision of the rail texture
    uint32 mUploadedMoveRevision = 0;                 // Block move revision the rail texture holds (53c2b)
    uint32 mUploadedRailRevision = 0;                 // Full re-share revision the rail texture holds (53c2b)
    int32 mFarBlockCount = 0;
    int32 mCpuBlockCount = 0;
    int32 mCpuTextureRows = 0;
    int32 mHullInstanceCounts[2] = {};
    int32 mHullUsed[2] = {};                          // Instances shown last frame (the range to rewrite)
    int32 mHullLowFrames[2] = {};                     // Frames in a row with the use far below the instance count
    TArray<FVector4f> mGroupTexels[3];                // Ring of group-texture upload buffers (render thread lag)
    TArray<FVector4f> mCpuTexels[3];                  // Ring of CPU-point upload buffers
    FUpdateTextureRegion2D mGroupRegions[3];          // Upload regions, one per ring slot (no per-frame allocation)
    FUpdateTextureRegion2D mCpuRegions[3];
    int32 mUploadRing = 0;
    double mGpuRailEpochS = 0.0;                      // Rail time the GPU copy of the rails is based at
    double mGpuRailEpochRealS = 0.0;                  // Platform time of the last re-base
    TArray<FTransform> mHullTransforms[2];            // Scratch per faction, reused every frame
    TArray<FTransform> mHullPrevTransforms[2];
    TArray<int32> mShipSlots;                         // Scratch: each CPU ship's slot in its layer this frame
    TArray<FVector4f> mPointRelM;                     // Scratch: each Point-tier CPU ship's drawn position relative to
                                                      // the viewpoint, w 1 when the CPU draws it (the Mid hand-over)
    TArray<int32> mMeshPoolOwners;                    // NPC index each pool mesh showed last frame (INDEX_NONE: none)
    TArray<int32> mMeshPoolShips;                     // Scratch: this frame's CPU ship index per used pool mesh, in
                                                      // placement order (their ribbon feeds, SDD 51 R7)
    TArray<float> mHullPlumeData[2];                  // Scratch per faction: SOL::ThrusterMaterialParams floats per
                                                      // instance slot, reused every frame (SDD 51 section 4.10)
    TArray<FIntPoint> mPlumeBlockRanges[2];           // Scratch: per placement task, the lowest and highest slot with a
                                                      // plume (X > Y: none)
    FIntPoint mPlumeRange[2] = { FIntPoint(1, 0), FIntPoint(1, 0) };        // This frame's lowest and highest slot
                                                                            // with a plume (X > Y: none)
    FIntPoint mPlumeWrittenRange[2] = { FIntPoint(1, 0), FIntPoint(1, 0) }; // Last frame's: outside it every
                                                                            // instance holds zeros
    double mPlumeFocalRefPx = 0.0;                    // 1080p-reference focal length of the last redraw
    bool mArePlumeMeshes = true;                      // The thruster hull meshes are applied (sol.NpcPlumes)
    TArray<int32> mLaneShipCounts;                    // Lane ships per group column of the population (SDD 38 D13)
    TArray<FVector4f> mLaneWindowTexels;              // Per lane slot: this frame's row 6 texel (scratch, SDD 53 5.4)
    FSOLNpcLaneFrame mLaneFrame;                      // This frame's lanes (sized once per population)
    FSOLGlowScaleFeed mLaneGlowFeed;                  // HUD glow scale last pushed to the lane material (SDD 37)
    double mLaneFlowClockS = 0.0;                     // Real seconds of dash flow (no warp, never held)
    int32 mLaneMeshVertexCount = 0;                   // Lane ribbon vertices of the current population
    bool mAreLanesShown = false;                      // Last frame's lanes shown
    bool mIsPointCrossfadeOn = false;                 // NpcParams4.x last pushed to the far points
    bool mHasPushedPointCrossfade = false;            // ...pushed at least once
    bool mAreLaneLinesDrawn = false;                  // Lane component visibility last applied
    bool mIsLaneOverlayDrawn = false;                 // Lane lines or tunnels drawn, as told to the orbit lines
    bool mIsLaneOverride = false;                     // Verification: the Lanes row is not read
    bool mIsLaneOverrideShown = true;                 // ...and the lanes are shown as this says
};
