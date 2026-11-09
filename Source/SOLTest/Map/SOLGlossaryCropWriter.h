/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Map/SOLMapCamera.h"

#include "CoreMinimal.h"

class APlayerController;
class ASOLShipPawn;
class USOLBodyRegistrySubsystem;
class USOLMapModeSubsystem;
class USOLRouteSubsystem;
class USOLShipSubsystem;
class USOLSimClockSubsystem;
class USOLTargetingSubsystem;

/**
 * The -SOLWriteGlossaryCrops run (SDD 33 section 9, D39; editor and development builds only). Started by the ship
 * pawn in flight, one step per frame, on the smoke settings slot with the engine settings snapshot (the player's
 * GameUserSettings.ini is given back). It pins a scene: the sim date (SOL::Help::GLOSSARY_CROP_SIM_DATE, where the
 * sim clock starts for this flag, held there from the ship's placement on, so the planets and the space-fixed map grid
 * stand still), the ship moved into empty space beside Earth and kept there, the map open in Jump mode with a fixed
 * camera per shot. It then takes one window screenshot with the UI at 1920x1080 per scene: the inner
 * orbits with Earth targeted (its orbit gold; the map hologram off so its grid does not cover the lines), an active
 * Patrol Loop with waypoints above and below the ecliptic (legs, arrows, the dashed closing leg, stems), an active
 * Move whose stems reach the grid, and a destination pick in progress (disc, height guide, marker); the orbit lines
 * show only in their own scene (console switches, the player's settings untouched). Once every shot is on disk it cuts
 * each SOL::Paths::GLOSSARY_CROPS square (SOLGlossary::CropRect) out of its scene's shot, saves it to
 * SOLPlayerGuide::CapturePath, and only when every crop succeeded moves them into Docs/Images (staged next to them
 * first, each old image backed up during the swap, so any failure leaves the old images) and removes the shots. A crop
 * that matches the image in place within the capture noise (SOLPlayerGuide::CropNoise) keeps that file
 * untouched, so an unchanged scene changes no file. It logs "GlossaryCrops WROTE <path> (<bytes> bytes)" or
 * "GlossaryCrops KEPT <path> (...)" per crop and "GlossaryCrops DONE", or "GlossaryCrops FAIL <reason>", then quits
 * (exit code 1 on a failure); Tools\WritePlayerGuide.ps1 reads those lines.
 */
class SOLTEST_API FSOLGlossaryCropWriter
{
public:

    // Caches the controller and subsystems and starts waiting for the view
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the run; returns true once it has finished (and requested the quit)
    bool Update(ASOLShipPawn& pawn);

private:

    // The run's stages, in order
    enum class EStage : uint8
    {
        WaitView,       // The view at the shots' size, the ship spawned, shaders compiled; then the date pinned
        Place,          // The ship placed (from the pinned date's positions) and the map opened
        Setup,          // The next scene set up
        Settle,         // ... scripted and settled, then captured
        Captured,       // A few frames so the capture shows this scene
        WaitFiles,      // Every shot on disk, then the crops cut and moved into place
    };

    // Moves to a stage, its time and frames counted from now
    void ToStage(EStage stage);

    // Returns the game viewport's size in pixels (zero without a controller)
    FIntPoint GetViewSize() const;

    // Parks the OS cursor in the view's top-right corner (out of every crop)
    void ParkCursor() const;

    // Fixes the scenes' view axes from Earth's orbit and the ship's offset from Earth; false when Earth or the Sun is
    // missing
    bool PlaceShip();

    // Holds the sim clock at the pinned date and the ship at its offset from Earth, at rest relative to it (no spin)
    void HoldScene() const;

    // Sets the scene's map camera, its focus following the scene's reference body
    void AimCamera() const;

    // Sets up the scene mScene; false (with a reason) on a failure
    bool SetupScene(ASOLShipPawn& pawn, FString& outReason);

    // Runs the scene's per-frame script (the destination pick's steps); returns true once it is ready to settle
    bool ScriptScene(ASOLShipPawn& pawn);

    // Returns whether the scene shows what its crops need (empty), else what is missing
    FString CheckScene() const;

    // Returns whether every shot is on disk, not empty and the same size as at the last check
    bool AreShotsWritten();

    // Cuts every crop out of its shot, keeps the images it matches within the noise and moves the others into place;
    // false (with a reason) on the first failure, the old images kept
    bool WriteCrops(FString& outReason) const;

    // Logs the failure and quits; nothing more is written
    void Fail(const FString& reason);

    // Logs the end and quits
    void Finish();

    TWeakObjectPtr<APlayerController> mController;     // The player's controller (the view size, the cursor, the HUD)
    TWeakObjectPtr<USOLMapModeSubsystem> mMapMode;
    TWeakObjectPtr<USOLRouteSubsystem> mRoutes;
    TWeakObjectPtr<USOLShipSubsystem> mShips;
    TWeakObjectPtr<USOLSimClockSubsystem> mSimClock;
    TWeakObjectPtr<USOLTargetingSubsystem> mTargeting;
    TWeakObjectPtr<USOLBodyRegistrySubsystem> mRegistry;
    TArray<int64> mShotSizes;                          // Each shot's file size at the last check (-1: not there)
    FSOLOrbitCameraState mCamera;                      // The scene's map camera (its focus as an offset, see below)
    FVector3d mFocusOffsetM = FVector3d::ZeroVector;   // The focus relative to mFocusBodyIndex (universe meters)
    FVector3d mShipOffsetM = FVector3d::ZeroVector;    // The ship relative to Earth (universe meters)
    FVector3d mForward = FVector3d::ForwardVector;     // The scenes' horizontal view direction (ecliptic, unit)
    FVector3d mRight = FVector3d::RightVector;         // ... and screen right (ecliptic, unit)
    EStage mStage = EStage::WaitView;                  // Current stage
    int32 mScene = 0;                                  // The scene in progress (index of the writer's scene table)
    int32 mFocusBodyIndex = INDEX_NONE;                // The body the map focus follows
    int32 mEarthIndex = INDEX_NONE;                    // Earth's body index (the ship and the waypoints follow it)
    int32 mSunIndex = INDEX_NONE;                      // The Sun's body index (the orbit scene's focus)
    int32 mScriptStep = 0;                             // The scene script's step
    int32 mStageFrames = 0;                            // Frames since the stage began
    int32 mViewFrames = 0;                             // Frames in a row at the wanted view size
    double mStageS = 0.0;                              // Seconds since the stage began
    double mPinnedJulianDate = 0.0;                    // SOL::Help::GLOSSARY_CROP_SIM_DATE as a Julian date
    double mRunS = 0.0;                                // Seconds since the run began (the watchdog)
    double mLastTimeS = 0.0;                           // Platform time of the last update
    double mLastFileCheckS = 0.0;                      // Stage time of the last shot size check
    bool mIsMapOpen = false;                           // The run opened the map
    bool mIsPlaced = false;                            // The date is pinned and the ship placed (held every frame)
    bool mIsFinished = false;                          // The quit has been requested
};
