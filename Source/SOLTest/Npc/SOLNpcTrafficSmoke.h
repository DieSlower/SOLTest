/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Npc/SOLNpcRail.h"

#include "CoreMinimal.h"

class ASOLNpcVisuals;
class ASOLOrbitLineVisuals;
class ASOLShipPawn;
class APlayerController;
class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLNpcSubsystem;
class USOLShipSubsystem;

/**
 * Verification-only NPC lane traffic script (-SOLSmokeNpcTraffic, SDD 50 section 8 item 2, V1-V5). It requests a 1M
 * population (unless one is already requested), detaches the lanes and tunnels from the Lanes overlay row (lane and
 * tunnel verification overrides, tunnels shown), the orbit lines from their saved switch (off) and turns the space dust
 * off in memory, so no player setting is read or written. After the population: V4 the halo share of every lane ship.
 * V1 (on the Earth-Mars axis a quarter of the way from Earth, looking toward Mars): keep right for up to 100 sampled
 * rails per lane (C++, and the Earth-Mars ones on screen), the tunnel texel radius against the frozen Rw (V4), two
 * screenshots 0.5 s apart with the sampled ships moved between them; V5 there: the float mirror of the point shader
 * against the CPU rail for ships near the Mid radius (case 10's bound), mid-lane float steps reported, three
 * screenshots. V2 (the lane's middle, flying 5 km past a coasting lane ship so it is promoted to the Full tier and
 * demoted again): Mid-tier ships (or sampled rails) at the speed field within the effort spread, rank correlation with
 * rho, wall ships at w, the Mid and Full hull headings drawn over the pass against LaneHullForward (at least one of
 * each), the per-frame nose change at promotion and demotion, the rails' heading chain (nose first, engine first, held
 * through the U-turn). V3 (side view of Earth's end ring): the turning ships beyond the ring and outside Earth by the
 * D14 clearance, V5 hand-over and 60 Hz smoothness of the end-zone ships, a screenshot. The V5 hand-over samples lie
 * within 0.5-2x the Mid radius (ships there now, plus sampled ships seen from viewpoints that make them cross it); the
 * CPU point's drawn position (ASOLNpcVisuals' blend) must step like the far point's within half a pixel at the Mid
 * radius, and every frame must draw the ship exactly once (the tiers' Mid radius plus the lane's hand-over margin, the
 * CPU point layer for a CPU ship the shader hides, the far layer otherwise). Each check logs a LogSOL PASS /
 * FAIL line, then a SUMMARY line, and the game quits. Screenshots are NpcTraffic_<w>x<h>_<view>.png.
 */
class SOLTEST_API FSOLNpcTrafficSmoke
{
public:

    // Caches the subsystems and actors, sets up the overrides and the population, and starts the first phase
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

private:

    // Script phases, in order
    enum class ESOLTrafficPhase : uint8
    {
        WaitPopulation,     // Until the population is uploaded and the group rows written (times out); then V4 halo
        V1Setup,            // The quarter point looking toward Mars settles; then V1, V4 radius, the first shot
        V1Hold,             // 0.5 s later: the second shot and the motion check
        V5Check,            // Hand-over against the float mirror, the first V5 shot
        V5Shot2,
        V5Shot3,
        V2Setup,            // Flying past a mid-lane ship (tracked every frame); then V2 and its shot
        V3Setup,            // The side view of Earth's end ring settles; then V3, V5 smoothness and the shot
        Tail,               // The last screenshot is written
        Done,
    };

    // Runs a phase's entry actions (the views), once the previous phase's screenshot is written
    void EnterPhase(ESOLTrafficPhase phase);

    // Runs a phase's exit checks and screenshots
    void ExitPhase(ESOLTrafficPhase phase);

    // Returns the minimum real duration of a phase in seconds
    static double GetPhaseDuration(ESOLTrafficPhase phase);

    // Logs the SUMMARY line and takes the closing screenshot (which quits)
    void Finish(ASOLShipPawn& pawn);

    // Finds the Earth-Mars lane and fills the per-lane samples and the Earth-Mars ship list; false without the lane
    bool BuildSamples();

    // Places the ship (universe ecliptic metres) facing a direction, at the reference velocity, not rotating
    void SetupShip(const FVector3d& positionM, const FVector3d& forward) const;

    // V4: the share of lane ships beyond the wall radius
    void CheckHalo();

    // V4: every shown tunnel's radius (feed and texel) equals its lane's frozen wall radius
    void CheckTunnelRadius();

    // V1: sampled rails on their direction's side of the axis (C++), the Earth-Mars ones on screen
    void CheckKeepRight();

    // V1: the sampled Earth-Mars ships moved between the two screenshots
    void CheckMotion();

    // V2: places the viewpoint where a sampled coasting mid-lane ship passes it at NPCTRAFFIC_V2_MISS_M, so the ship is
    // promoted to the Full tier and demoted again during the phase
    void SetupFlyBy();

    // V2, every frame of the fly-by: the Mid and Full lane hulls drawn against LaneHullForward and the tracked ship's
    // per-frame nose change at promotion and demotion
    void TrackFlyBy();

    // V2: speeds against the mean field and the effort spread, rank correlation, wall speed, hull headings
    void CheckSpeeds();

    // V3: the turning ships beyond Earth's end ring and outside Earth by the D14 clearance
    void CheckUTurn();

    // V5: the float mirror against the CPU rail for ships near the Mid radius; with bSmooth also the 60 Hz smoothness
    // of the end-zone and U-turn ships near the viewpoint
    void CheckHandOver(const TCHAR* label, bool bSmooth);

    // Requests a screenshot with the HUD named NpcTraffic_<width>x<height>_<name>.png
    void TakeShot(const TCHAR* name) const;

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    TWeakObjectPtr<ASOLNpcVisuals> mVisuals;
    TWeakObjectPtr<ASOLOrbitLineVisuals> mOrbitLines;
    TWeakObjectPtr<USOLNpcSubsystem> mNpcs;
    TWeakObjectPtr<USOLShipSubsystem> mShips;
    TWeakObjectPtr<USOLBodyRegistrySubsystem> mRegistry;
    TWeakObjectPtr<USOLAnchorSubsystem> mAnchor;
    TWeakObjectPtr<APlayerController> mController;
    ESOLTrafficPhase mPhase = ESOLTrafficPhase::WaitPopulation;
    double mPhaseTimeS = 0.0;
    double mTotalTimeS = 0.0;
    int32 mPhaseFrames = 0;
    bool mIsViewPlaced = false;             // The current phase's entry actions have run
    int32 mLane = INDEX_NONE;               // The Earth-Mars lane
    TArray<TArray<int32>> mSamples;         // Per lane: up to NPCTRAFFIC_SAMPLES rail indices, spread over the lane
    TArray<int32> mLaneShips;               // Every Earth-Mars rail index
    TArray<FVector3d> mShotPositionsM;      // V1: the sampled Earth-Mars positions at the first screenshot
    double mShotRailTimeS = 0.0;            // ...and the rail time then
    FSOLNpcRailFrame mLastFrame;            // The rail frame of the last update and the one before it (the tracked
    FSOLNpcRailFrame mPrevFrame;            // ship's universe velocity)

    // V2 fly-by statistics, accumulated every frame
    struct FFlyBy
    {
        int32 Npc = INDEX_NONE;             // The tracked lane ship (rail index)
        uint8 PrevTier = 0xFF;              // Its AI tier last frame (0xFF: not a CPU ship)
        FVector3f PrevHeading = FVector3f::ZeroVector;
        int32 Frames = 0;                   // Frames it was a CPU ship with a hull heading (hull or flame-data dot)
        int32 DotFrames = 0;                // ...of them drawn as a dot (SDD 54 4.7: its flame follows the heading)
        double FirstHeadingM = 0.0;         // Its distance in the first frame with a heading (the flame-data gate)
        int32 Promotions = 0;
        int32 Demotions = 0;
        double PromoteDeg = 0.0;            // Worst nose change in a promotion / demotion frame
        double DemoteDeg = 0.0;
        double FrameDeg = 0.0;              // ...and in any other frame
        double ClosestM = TNumericLimits<double>::Max();
        double RelativeSpeedMps = 0.0;      // Its speed relative to the viewpoint at the setup
        int32 MidShips = 0;                 // Mid / Full lane ships with a hull heading (hulls and flame-data dots),
        int32 MidDots = 0;                  // summed over the frames, and the dots among them
        double MidWorstDeg = 0.0;
        int32 FullShips = 0;
        int32 FullAligned = 0;
        int32 FullReversed = 0;
        double FullWorstDeg = 0.0;
    };
    FFlyBy mFlyBy;
    double mPinnedFullRadiusM = 0.0;        // V2: the Full AI radius pinned in memory at the start (0: not pinned)
    int32 mChecksRun = 0;
    int32 mChecksPassed = 0;
};
