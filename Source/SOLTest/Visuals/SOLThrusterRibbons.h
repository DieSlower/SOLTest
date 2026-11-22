/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Visuals/SOLThrusterMath.h"

#include "ConvexVolume.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include <atomic>

#include "SOLThrusterRibbons.generated.h"

class APlayerCameraManager;
class ULocalPlayer;
class UNiagaraComponent;
class UNiagaraSystem;
class USOLSettingsSubsystem;
class USOLThrusterRibbonComponent;

// One ship's (or player nozzle's) flame input for a frame (SDD 51 R7, R10-8): written by a feeder, committed into the
// shared pose store. Every position is render-space metres in Unreal axes; the pose's directions likewise
struct FSOLRibbonFeed
{
    FVector3d ShipPositionM = FVector3d::ZeroVector;    // Ship centre now
    FSOLPlumeSample Pose;                               // Nozzle exit offset from the ship centre, unit exhaust
                                                        // direction and b now (0 when the plume is off); its EmitTimeS
                                                        // is set on commit
    double PlumeLengthM = 0.0;                          // L_p now; 0 when the plume is off
    double DistanceM = 0.0;                             // Camera to the ship (the pixel rule)
    float ColourScalar = 0.0f;                          // c now
    float NozzleRadiusM = 0.0f;
    int32 Key = INDEX_NONE;                             // NPC index; player nozzles use PlayerRibbonKey
    bool bValid = false;                                // The slot holds a feed this frame
};

// One NPC ribbon drawn last frame, as the verification getter reports it
struct FSOLRibbonDrawnTrail
{
    FVector3d ShipPositionM = FVector3d::ZeroVector;    // Ship centre, render-space metres
    double RibbonPx = 0.0;                              // On-screen nominal length (reference px)
    double DistanceM = 0.0;                             // Camera to the ship
    int32 Key = INDEX_NONE;                             // NPC index
    bool bNiagara = false;                              // Drawn by the Niagara set (else by the strip pool)
};

/**
 * The one-piece thruster flames (SDD 51 sections 11.1 R1-R9 and 11.2 R10): one shared pose store and both renderers.
 * Feeders (the NPC placement pass through BeginNpcFeeds / CommitNpcFeeds for ships inside last frame's view frustum,
 * the player pawn through FeedPlayer) hand in each ship's nozzle pose once per frame; the store keeps one
 * FSOLPlumeHistory per owner in a reserved side pool (never a Mass fragment, so it survives tier switches) keyed by NPC
 * index or player nozzle, pushes a pose at most every FLAME_SAMPLE_INTERVAL_S and runs the FLAME_SHUTDOWN_S spool-down
 * (b falls to 0, L_p and c held). Once per frame, in TG_PostUpdateWork (after the universe update and the camera,
 * before Niagara's TG_LastDemotable tick, so both renderers show the same frame), it releases flames whose gas has left
 * (SOLThruster::RibbonTrailReleasable), applies the pixel rule, splits the shown NPC flames by pixel size
 * (SplitRibbonRenderers) and draws: the player's flames and the longest NPC flames through a fixed pool of
 * RIBBON_NIAGARA_POOL_TRAILS NS_SOLThrusterTrails components (a FLAME_TUBE_SIDES-sided tube ribbon each, created once,
 * active only while drawing; widths floored on the CPU with the device focal), every other shown flame through the GPU
 * strip pool (USOLThrusterRibbonComponent, the same tube, floored on the GPU). Both take their spine from
 * SOLThruster::BuildFlamePoints relative to one frame anchor near the camera (RibbonAnchorCm) and shade it with the same
 * SOLTH_FlamePixel. Clock: the world's game time (R8). Developer cvars: sol.ThrusterRibbons (0 hides both renderers
 * and skips the work) and sol.ThrusterRibbonNiagaraPx (the Niagara threshold override).
 */
UCLASS()
class SOLTEST_API ASOLThrusterRibbons : public AActor
{
    GENERATED_BODY()

public:

    // Ticks once per frame in TG_PostUpdateWork; creates the root and the strip pool component
    ASOLThrusterRibbons();

    // Returns the key of a player nozzle in the store (negative, never an NPC index)
    static constexpr int32 PlayerRibbonKey(const int32 nozzle) { return -1 - nozzle; }

    // Opens this frame's NPC feeds (game thread): returns a view of slotCount pending slots (grow-only; only the slots
    // FeedNpc lists are read back, so a slot is written through FeedNpc, never directly) and fixes the feed clock, so
    // FeedNpc may run in parallel until CommitNpcFeeds
    TArrayView<FSOLRibbonFeed> BeginNpcFeeds(int32 slotCount);

    // Returns the store slot of a key, INDEX_NONE when it has no trail; read-only, so parallel feeders may call it
    // between BeginNpcFeeds and CommitNpcFeeds (only CommitNpcFeeds adds or removes trails)
    int32 FindTrail(const int32 key) const
    {
        const int32* slot = mTrailByKey.Find(key);
        return slot != nullptr ? *slot : INDEX_NONE;
    }

    // Feeds one NPC between BeginNpcFeeds and CommitNpcFeeds, from a parallel task: a ship with a trail (slot from
    // FindTrail) is committed in place (each key, so each slot, is fed by one task only); a burning ship without one is
    // copied into its pending slot (outPending, an element of BeginNpcFeeds' view) and listed for CommitNpcFeeds
    void FeedNpc(int32 slot, const FSOLRibbonFeed& feed, FSOLRibbonFeed& outPending);

    // Creates the trails of the pending NPC feeds FeedNpc listed since BeginNpcFeeds, in slot order (game thread,
    // serial; only the listed slots are visited); dropped while sol.ThrusterRibbons is 0
    void CommitNpcFeeds();

    // Commits one player nozzle's feed (game thread); its key is set from the nozzle; dropped while the ribbons are off
    void FeedPlayer(int32 nozzle, const FSOLRibbonFeed& feed);

    // Returns whether the ribbons are on (sol.ThrusterRibbons); feeders skip their work when not
    static bool AreRibbonsEnabled();

    // Returns the vanish length V of the current quality preset (R3; game thread, read before a parallel feed)
    double GetVanishPx() const;

    // Returns whether a sphere (render-space metres) lies at least partly inside last frame's view frustum, true
    // before the first frame or without a viewport (R10-Q8: only ships on screen are fed); read-only, so parallel
    // feeders may call it between BeginNpcFeeds and CommitNpcFeeds
    bool IsOnScreen(const FVector3d& centreM, double radiusM) const;

    // Returns whether a sphere (render-space metres, a negative or NaN radius read as 0) lies at least partly inside a
    // view frustum (render-space cm planes); true when bHasFrustum is false (IsOnScreen's test, pure)
    static bool SphereTouchesFrustum(const FConvexVolume& frustum, bool bHasFrustum, const FVector3d& centreM,
        double radiusM);

    // Verification: a fixed history clock (nowS >= 0; a negative value restores the world's game time)
    void SetVerificationClock(double nowS);

    // Verification: a feeder run at the start of each render, after the rebase and the camera update, with the camera
    // in render-space metres (a feed made earlier in the frame would be a frame of ship motion and any rebase behind);
    // an empty function removes it
    void SetVerificationFeeder(TFunction<void(const FVector3d& cameraM)> feeder);

    // Verification: seeds an owner's history (created when missing)
    void SeedVerificationHistory(int32 key, const FSOLPlumeHistory& history);

    // Returns the trails Niagara drew last frame, the player's included (verification)
    int32 GetNiagaraTrailCount() const { return mNiagaraTrailsDrawn; }

    // Returns the trails the strip pool drew last frame (verification)
    int32 GetPoolTrailCount() const { return mPoolTrailsDrawn; }

    // Returns the live histories of the store after last frame's release pass (verification)
    int32 GetLiveTrailCount() const { return mLiveTrails; }

    // Returns the device focal length (device px) of last frame's 1 px floor (verification)
    double GetLastDeviceFocalPx() const { return mLastDeviceFocalPx; }

    // Returns last frame's anchor in render-space cm (verification)
    FVector3d GetLastAnchorCm() const { return mLastAnchorCm; }

    // Returns the game-thread time of last frame's render in ms (verification, the A/B profile)
    double GetLastTickMs() const { return mLastTickMs; }

    // Returns one phase of last frame's render in ms (verification: ESOLRibbonPhase order)
    double GetLastPhaseMs(const int32 phase) const { return mLastPhaseMs[FMath::Clamp(phase, 0, RIBBON_PHASES - 1)]; }

    // Returns the fed, shown-by-the-pixel-rule NPC trails left undrawn last frame because their bounds were outside
    // the view frustum (verification)
    int32 GetCulledTrailCount() const { return mCulledTrails; }

    // Verification phases of GetLastPhaseMs: the NPC feed commit, the store pass (release, pixel rule, frustum), the
    // split, the Niagara pushes, the strip pool (fill and upload); within the pushes, the array and count setters and
    // the moves; the frame setup (camera, projection, frustum); within the strip pool, its texel fill; within the
    // pushes, the point build
    static constexpr int32 RIBBON_PHASES = 10;

    // Appends the NPC ribbons drawn last frame by each renderer (verification)
    void GetDrawnNpcTrails(TArray<FSOLRibbonDrawnTrail>& outTrails) const;

    // Returns the Niagara set components active last frame (verification)
    int32 GetActiveNiagaraCount() const;

    // Returns the GPU strip pool (verification: its blocks and last texels)
    const USOLThrusterRibbonComponent* GetStripPool() const { return Pool.Get(); }

    // Renders this frame's ribbons
    virtual void Tick(float deltaSeconds) override;

protected:

    // Loads the system, creates the Niagara set, initialises the pool and reserves the store
    virtual void BeginPlay() override;

private:

    // One owner's per-frame state (the hot part of a store slot; its history is the same index of mHistories, so the
    // per-frame store pass never strides over the sample rings); cache-line aligned, so parallel feeders writing
    // neighbouring slots never share a line
    struct alignas(PLATFORM_CACHE_LINE_SIZE) FSOLRibbonTrail
    {
        FVector3d ShipPositionM = FVector3d::ZeroVector;
        FSOLPlumeSample Head;                           // The live nozzle pose (b after the spool-down), time now
        double PlumeLengthM = 0.0;                      // L_p, held at its last burning value
        double DistanceM = 0.0;
        double RibbonPx = 0.0;
        double PixelFade = 0.0;
        double NewestBurningS = 0.0;                    // Last time the flame emitted gas with b > 0
        double CutoffS = 0.0;                           // When the thrust cut out (valid while bCutoff)
        uint64 FedFrame = 0;
        float LastBurningBrightness = 0.0f;             // b before the cut-off (the spool-down's start)
        float ColourScalar = 0.0f;                      // c, held at its last burning value
        float NozzleRadiusM = 0.0f;
        int32 SampleCount = 0;                          // Poses stored in the history
        int32 Key = INDEX_NONE;
        int32 LiveIndex = INDEX_NONE;                   // Position in mLiveSlots while live
        bool bLive = false;
        bool bCutoff = false;                           // The thrust is off: spooling down or dark
    };

    // Finds or creates the store slot of a key; INDEX_NONE when the store cannot grow
    int32 AcquireTrail(int32 key);

    // Frees a store slot
    void ReleaseTrail(int32 slot);

    // Finds or (when burning) creates a feed's trail, then commits it (CommitFeedToSlot); game thread, serial
    void CommitFeed(const FSOLRibbonFeed& feed, double nowS);

    // Copies a feed into an existing trail, runs the spool-down and pushes its pose when due (that trail only)
    void CommitFeedToSlot(int32 slot, const FSOLRibbonFeed& feed, double nowS);

    // Returns the history clock (the verification clock or the world's game time)
    double NowS() const;

    // Hides both renderers (once) and drops the feeds
    void HideAll();

    // Marks a Niagara set component used this frame and resumes it when parked; returns false on its resume frame (the
    // caller then draws the flame with the strip pool and gives the component no points)
    bool UseNiagaraComponent(int32 component, double nowS);

    // Parks a Niagara set component: paused (no simulation while idle) and hidden
    void ParkNiagaraComponent(int32 component);

    // Writes one Niagara component's arrays from a store slot's trail (or zero points for INDEX_NONE); the header and
    // the point count only when they change
    void PushNiagaraTrail(int32 component, int32 slot, double nowS, const FVector3d& cameraCm, double deviceFocalPx);

    // Hands one flame's three arrays to a Niagara set component (the array interfaces' proxies are not reachable from
    // a game module: their helper header is private to Niagara)
    void SetNiagaraArrays(int32 component);

    // Updates last frame's view data from the camera: the projection (ULocalPlayer::GetProjectionData, refreshed only
    // when the viewport size, field of view or aspect settings change), the device focal length, the aspect and the
    // view frustum from the camera's current location and rotation; false without a viewport
    bool UpdateView(const ULocalPlayer& localPlayer, const APlayerCameraManager& cameraManager,
        const FVector3d& cameraCm);

    UPROPERTY(Transient)
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(Transient)
    TObjectPtr<USOLThrusterRibbonComponent> Pool;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UNiagaraComponent>> NiagaraTrails;

    UPROPERTY(Transient)
    TObjectPtr<USOLSettingsSubsystem> Settings;

    TArray<FSOLRibbonTrail> mTrails;                    // The shared store's per-frame state (reserved, grow-only)
    TArray<FSOLPlumeHistory> mHistories;                // The shared store's pose rings, one per slot of mTrails
    TArray<int32> mLiveSlots;                           // The live store slots (dense, swap-removed on release)
    TArray<uint8> mLiveClass;                           // Scratch: the store pass's class per live entry
    TArray<int32> mFreeTrails;                          // Free store slots
    TMap<int32, int32> mTrailByKey;                     // Owner key -> store slot
    TArray<FSOLRibbonFeed> mNpcFeeds;                   // This frame's NPC feed slots (grow-only)
    int32 mNpcFeedCount = 0;
    TArray<int32> mPendingFeeds;                        // Feed slots of this frame's new trails (grow-only)...
    std::atomic<int32> mPendingFeedCount{ 0 };          // ...and their count, appended by the parallel feeders
    double mFeedNowS = 0.0;                             // History clock of this frame's NPC feeds (BeginNpcFeeds)
    TArray<FSOLRibbonShown> mShown;                     // This frame's shown NPC ribbons (NpcIndex = store slot)
    TArray<int32> mNiagaraSlots;                        // Split output: store slots drawn by Niagara
    TArray<int32> mPoolSlots;                           // Split output: store slots drawn by the strip pool
    TArray<int32> mPlayerSlots;                         // This frame's player trails (store slots)
    TArray<FVector3f> mNiagaraPositionsCm;              // One flame's arrays (FLAME_DRAWN_POINTS long,
    TArray<FLinearColor> mNiagaraColours;               //  so the Niagara array data never reallocates)
    TArray<float> mNiagaraWidthsCm;
    TArray<int32> mNiagaraPointCounts;                  // Point count each component holds (zeroed once when unused)
    TArray<FVector4f> mNiagaraHeaders;                  // Flame header each component holds
    TArray<double> mNiagaraUsedS;                       // History clock each component last drew a trail at
    TArray<bool> mNiagaraActive;                        // Whether each component is active
    TFunction<void(const FVector3d&)> mVerificationFeeder;
    double mVerificationNowS = -1.0;
    double mLastDeviceFocalPx = 0.0;
    double mLastTickMs = 0.0;
    double mLastPhaseMs[RIBBON_PHASES] = {};
    int32 mCulledTrails = 0;
    int32 mNiagaraPlayerDrawn = 0;                      // Player trails Niagara drew last frame
    int32 mNiagaraNpcDrawn = 0;                         // Entries of mNiagaraSlots given a component last frame
    FVector3d mLastAnchorCm = FVector3d::ZeroVector;
    bool mHasAnchor = false;                            // mLastAnchorCm holds an anchor (RibbonAnchorHoldCm)
    FConvexVolume mFrustum;                             // Last frame's view frustum (render-space cm)
    bool mHasFrustum = false;
    FMatrix mProjectionMatrix = FMatrix::Identity;      // The cached projection (UpdateView) and what it was made for
    FIntRect mProjectionRect;
    FIntPoint mProjectionViewportSize = FIntPoint::ZeroValue;
    float mProjectionFovDeg = 0.0f;
    float mProjectionAspect = 0.0f;
    bool mProjectionConstrained = false;
    bool mHasProjection = false;
    double mViewAspect = 0.0;                           // Last frame's view aspect (the constrained rectangle's)
    int32 mNiagaraTrailsDrawn = 0;
    int32 mPoolTrailsDrawn = 0;
    int32 mLiveTrails = 0;
    bool mIsHidden = false;
};
