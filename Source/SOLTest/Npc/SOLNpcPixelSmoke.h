/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

class ASOLNpcVisuals;
class ASOLOrbitLineVisuals;
class ASOLShipPawn;
class ASOLThrusterRibbons;
class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLNpcSubsystem;
class USOLShipSubsystem;
struct FImage;

/**
 * Verification-only NPC sub-pixel ship script (-SOLSmokeNpcPixel, SDD 54 section 9.6, cases a-f). It requests a 1M
 * population (unless one is already requested) and turns the lanes, tunnels, orbit lines and space dust off in memory,
 * so no player setting is read or written. At the spawn view (the default battles, combat on) over a scan window:
 * (a) every CPU ship's render tier against its true size (the budgeted radii from the pixel thresholds, hysteresis
 * band either tier) and the unbudgeted radii against SOLNpcPixel::TierRadiusM; (b) no mesh or instanced hull under the
 * hull threshold over (1 + hysteresis); (c) the far layer shown exactly when SOLNpcPixel::FarLayerShown says (dark in
 * flight); (f) a dot-drawn ship with thrust inside the plume gate has a drawn flame. Then (c) with the camera FOV
 * locked narrow (the far layer turns on). (e) The viewpoint 40 km outside a battle: the nearest ships are under 1 px,
 * still not bHidden, resolvable by the interaction subsystem and given overlay icons (by the HUD, or by a local icon
 * layer when the player's overlays hide ship icons). (d) After a respawn with one sparse battle and combat, plumes and
 * flames off, a battle ship ridden at true sizes 0.8, 1.5 and 3 px (the game's anti-aliasing, TSR, as the player sees
 * it: with it off a 1.5 ref px dot at 720p is the 1 device px floor and its triangle misses the pixel centres;
 * -SOLNpcPixelAaOff for crisp pixels at screen percentage 100; no HUD; projected through the pawn's camera): its pixel
 * equals the background at 0.8 px, is lit at 1.5 px and brighter at 3 px (the mirror's alpha 0, 0.5, 1), its summed
 * excess about alpha x area of the 3 px one. Last the jump map: (c) on (Q9), a screenshot, the population despawned
 * and the same view again; the NPCs must brighten many pixels in several places (their dots overlap at map scale),
 * then (c) per the rule once the map is closed. Each check logs a LogSOL PASS / FAIL line, then a SUMMARY line, and
 * the game quits. Its own screenshots (NpcPixel_<w>x<h>_<name>.png) are deleted once measured (kept with
 * -SOLNpcPixelKeepShots).
 */
class SOLTEST_API FSOLNpcPixelSmoke
{
public:

    // Caches the subsystems and actors, sets up the overrides and the population, and starts the first phase
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

private:

    // Script phases, in order
    enum class ESOLPixelPhase : uint8
    {
        WaitPopulation,     // Until the population is uploaded (times out)
        SpawnScan,          // (a), (b), (c), (f) every frame at the spawn view
        FovLocked,          // The camera FOV locked narrow: (c) the far layer turns on
        FovUnlocked,        // Unlocked again: (c) off
        HiddenSetup,        // 40 km outside a battle: (e)
        Respawn,            // One sparse battle: waits for the new population
        ShotFar,            // (d) at 0.8 px
        ShotFade,           // (d) at 1.5 px
        ShotFull,           // (d) at 3 px
        ShotMeasure,        // The three screenshots measured
        MapOpen,            // The jump map open: (c) on, then its screenshot
        MapEmpty,           // The population despawned: the same map view's screenshot without NPCs
        MapMeasure,         // The pair compared (the NPC dots); the map closed
        MapClosed,          // (c) the rule again (the population is gone)
        Done,
    };

    // One (d) screenshot: what the CPU side expected when it was requested
    struct FShot
    {
        FString Path;
        double SizePx = 0.0;                // True size from the viewpoint distance and the live focal
        double Alpha = 0.0;                 // The mirror's dot alpha
        FVector2D ScreenPx = FVector2D::ZeroVector;
        bool bProjected = false;
        uint8 RenderTier = 0xFF;
    };

    // Runs a phase's entry actions
    void EnterPhase(ESOLPixelPhase phase);

    // Runs a phase's exit checks
    void ExitPhase(ESOLPixelPhase phase);

    // Returns the minimum real duration (s) of a phase
    static double GetPhaseDuration(ESOLPixelPhase phase);

    // Logs the SUMMARY line, restores the cvars and the camera, and takes the closing screenshot (which quits)
    void Finish(ASOLShipPawn& pawn);

    // Returns the camera manager's horizontal FOV in degrees
    double LiveFovDeg() const;

    // Returns the live focal length in reference px (the camera manager's FOV, the viewport aspect)
    double LiveFocalRefPx() const;

    // Returns the render tier threshold scale of the saved quality preset (SDD 54 Q12)
    double QualityScale() const;

    // (a), (b), (c), (f) for one frame
    void ScanFrame();

    // (c): the far layer's flag against SOLNpcPixel::FarLayerShown now; counted under the given expectation
    void CheckFarLayer(const TCHAR* label, bool bExpected);

    // Picks the battle ship farthest along a direction (live, inside a battle group); INDEX_NONE if none
    int32 PickBattleShip(const FVector3d& direction) const;

    // Returns the CPU ship index of an NPC this frame, or INDEX_NONE
    int32 FindCpuShip(int32 npcIndex) const;

    // Starts riding with an NPC at a distance along an offset direction (the camera looking back at it)
    void StartRide(int32 npcIndex, double distanceM, const FVector3d& offsetDir);

    // Pins the ship at the ride's offset from the target, at the target's velocity, facing it
    void PinRide();

    // Returns the unit direction away from Earth at the viewpoint (the Sun avoided), the battles' empty side
    FVector3d AwayFromEarth() const;

    // (e): the nearest ships under 1 px keep bHidden 0, resolve and have overlay icons
    void CheckHiddenSelectable();

    // Requests a screenshot without the HUD named NpcPixel_<width>x<height>_<name>.png; returns its path
    FString TakeShot(const TCHAR* name) const;

    // Projects a universe position through the pawn's camera component from the viewpoint-relative geometry; false if
    // off screen or behind
    bool ProjectFromPawnCamera(const FVector3d& positionM, FVector2D& outScreenPx) const;

    // Loads a screenshot as 8-bit BGRA; false if missing or unreadable
    static bool LoadShot(const FString& path, FImage& outImage);

    // Measures the brightest channel around a pixel: the peak over a window of the given half size, the median of a
    // ring around it (the background), the summed excess and the pixels above half the peak; false off the image
    static bool MeasureSpot(const FImage& image, const FVector2D& centerPx, int32 radius, double& outPeak,
        double& outBackground, double& outExcess, int32& outLitPixels);

    // Q9 and SDD 54 4.8: the NPC dots on the jump map, the map screenshot against the one without NPCs
    void MeasureMapShots();

    // (d): the three ride screenshots against the mirror's alpha
    void MeasureRideShots();

    // Sets a console variable from a string, logging a name that does not exist
    static void SetCvar(const TCHAR* name, const TCHAR* value);

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    TWeakObjectPtr<ASOLNpcVisuals> mVisuals;
    TWeakObjectPtr<ASOLOrbitLineVisuals> mOrbitLines;
    TWeakObjectPtr<ASOLThrusterRibbons> mRibbons;
    TWeakObjectPtr<USOLNpcSubsystem> mNpcs;
    TWeakObjectPtr<USOLShipSubsystem> mShips;
    TWeakObjectPtr<USOLBodyRegistrySubsystem> mRegistry;
    TWeakObjectPtr<USOLAnchorSubsystem> mAnchor;
    TWeakObjectPtr<ASOLShipPawn> mPawn;
    ESOLPixelPhase mPhase = ESOLPixelPhase::WaitPopulation;
    double mPhaseTimeS = 0.0;
    double mTotalTimeS = 0.0;
    int32 mPhaseFrames = 0;
    bool mIsShotTaken = false;              // The current phase's screenshot was requested...
    double mShotHoldS = 0.0;                // ...and the real time left for it to be written

    // The scan (a), (b), (c), (f)
    int32 mScanFrames = 0;
    int32 mTierChecked = 0;                 // Ship-frames whose tier was checked outside the hysteresis bands
    int32 mTierWrong = 0;
    int32 mTierBand = 0;                    // ...and inside a band (either tier)
    FString mTierWorst;                     // The first wrong ship
    int32 mRadiusChecked = 0;               // Frames whose unbudgeted radius matched TierRadiusM (per tier)...
    int32 mRadiusWrong = 0;
    int32 mMeshBound = 0;                   // ...and frames with a binding pool (skipped, a reported limit)
    int32 mHullBound = 0;
    double mRadiusWorstRel = 0.0;
    int32 mHullShips = 0;                   // (b) mesh and instanced ship-frames...
    int32 mHullTooSmall = 0;                // ...under the kept threshold
    double mHullMinPx = TNumericLimits<double>::Max();
    int32 mMeshShips = 0;
    int32 mDotShips = 0;
    int32 mFarChecked = 0;                  // (c) frames and mismatches per label
    int32 mFarWrong = 0;
    int32 mBurningDots = 0;                 // (f) dot ship-frames with thrust and flame data...
    int32 mBurningDotsDrawn = 0;            // ...whose flame was drawn last frame
    TSet<int32> mBurningDotShips;           // Distinct NPCs of them, and those with a drawn flame
    TSet<int32> mDrawnDotShips;

    // The ride
    int32 mTarget = INDEX_NONE;             // The NPC ridden with (population index)
    double mRideDistanceM = 0.0;
    FVector3d mRideOffsetDir = FVector3d::ZeroVector;   // Unit ecliptic offset from the target to the viewpoint
    FVector3d mRideUpDir = FVector3d::ZeroVector;       // Unit ecliptic screen up
    FVector3d mTargetPrevM = FVector3d::ZeroVector;
    double mTargetPrevRailS = 0.0;
    bool mHasTargetPrev = false;
    FVector3d mLineOfSight = FVector3d::ZeroVector;     // (d): the camera's look direction (away from Earth)

    // The screenshots
    FString mMapShot;                       // The map with the population...
    FString mMapEmptyShot;                  // ...and without it
    FShot mShots[3];
    int32 mRespawnStartTotal = 0;
    FString mSavedAa;                       // Console variables restored at the end
    FString mSavedScreenPercentage;
    int32 mChecksRun = 0;
    int32 mChecksPassed = 0;
};
