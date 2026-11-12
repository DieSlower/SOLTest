/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

class ASOLNpcVisuals;
class ASOLOrbitLineVisuals;
class ASOLShipPawn;
class USOLBodyRegistrySubsystem;
class USOLMenuSubsystem;
class USOLNpcSubsystem;
class USOLSettingsSubsystem;
class USOLShipSubsystem;
class USOLSimClockSubsystem;

/**
 * Verification-only NPC lane-line script (-SOLSmokeNpcLanes, SDD 38 section 8 item 2). It requests a 1M population
 * (unless one is already requested), detaches the lanes from the Lanes overlay row and the orbit lines from their
 * saved switch (both verification overrides: the player's settings are neither read nor written for them; the orbit
 * lines are turned off so the lanes alone must keep the shared occluders current, D24), turns the space dust off
 * (sol.SpaceDust, in memory; its streaks would cover the lanes after each teleport) and sets the HUD glow as a
 * pending value only (never applied, reverted at the end). For six views (V1 the start 10,000 km above Earth facing
 * the Moon, V2 0.15 AU off Venus, V3 3 AU above the Sun, V4 halfway along the Earth-Mars lane, V4b there one lane
 * radius off its centre line looking 1.5 degrees off its axis (near-plane regression view), V5 beside Earth with the
 * Earth-Moon lane leaving from behind its disc) it shoots lanes on at 100%, 0% and 200% HUD glow and lanes off
 * (NpcLanes_<width>x<height>_<view>_<variant>.png in Saved/Screenshots), checking each time that the lane component's
 * visibility equals (lanes shown and a lane drawn), that the far points' crossfade flag follows, that the lane
 * material's glow scale is HudGlowScale of the pending value, and once that every lane's group-texel .w values are its
 * radius and weight from a fresh histogram (CountLaneShips) and BuildLaneFrame. Then: the dash flow advances at the
 * real-time rate under a 4-step time warp, keeps flowing at that rate while the pause menu shows (it never freezes,
 * D15 amended 2026-11-11) and after the resume; sol.NpcLanes 0 hides the lines and restores the points (screenshot);
 * the lanes stay visible on the jump map (D18, screenshot); the reverted glow returns. Each check logs a LogSOL PASS /
 * FAIL line, then a SUMMARY line, and the game quits. With -SOLSmokeNpcLanesProfile (standalone -game only) it skips
 * the views and checks after the population and, for V2 and V3, captures stat dumpave of STAT_SOL_NpcRedraw (with its
 * STAT_SOL_NpcLanes share) over 240 frames and three ProfileGPU frames inside that window with the lanes drawn and
 * again with sol.NpcLanes 0 (SDD 38 section 8 item 7); the numbers are read from the log.
 */
class SOLTEST_API FSOLNpcLaneSmoke
{
public:

    // Caches the subsystems and actors, sets up the overrides and the population, and starts the first phase
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

private:

    // Script phases, in order (the view and variant phases repeat per view and variant)
    enum class ESOLLanePhase : uint8
    {
        WaitPopulation,     // Until the population is uploaded and lanes are drawn (repeats; times out)
        ViewSetup,          // The ship moves to the next view
        VariantSet,         // Lanes on / off and the pending HUD glow of the variant (checked at the end)
        VariantShot,        // ...its screenshot; then the next variant, or the next view
        Flow,               // Time warp up: the flow phase advances at the real-time rate
        PauseOpen,          // The pause menu opens
        PauseHold,          // ...the flow phase keeps advancing at the real-time rate (never frozen)
        Resume,             // ...and after the resume (time warp reset)
        CvarOff,            // sol.NpcLanes 0: lines hidden, points restored (screenshot)
        CvarShotWait,       // ...the screenshot is written before the cvar returns
        CvarOn,             // sol.NpcLanes 1 again
        MapOpen,            // The jump map: lanes still drawn (D18, screenshot)
        MapShotWait,        // ...the screenshot is written before the map closes
        MapClose,
        GlowRevert,         // The pending HUD glow reverted: the saved glow scale is back
        ProfileSetup,       // Profile flag only: the ship moves to the next profile view and settles
        ProfileOn,          // ...stat dumpave and ProfileGPU with the lanes drawn
        ProfileOff,         // ...and again with sol.NpcLanes 0
        Done,
    };

    // Runs a phase's entry actions
    void EnterPhase(ESOLLanePhase phase, ASOLShipPawn& pawn);

    // Runs a phase's exit checks
    void ExitPhase(ESOLLanePhase phase, ASOLShipPawn& pawn);

    // Returns the next phase after one has ended (moving to the next view after its last variant)
    ESOLLanePhase NextPhase(ESOLLanePhase phase);

    // Returns the minimum real duration of a phase in seconds
    static double GetPhaseDuration(ESOLLanePhase phase);

    // With the profile flag: runs one cost capture (stat dumpave, three ProfileGPU frames) from the first frame it is
    // called; returns true once it is done
    bool TickProfile(const TCHAR* label, UWorld* world);

    // Logs the SUMMARY line and takes the closing screenshot (which quits)
    void Finish(ASOLShipPawn& pawn) const;

    // Moves the ship to a view (Unreal-handed universe metres) facing a direction, at the reference velocity
    void SetupShip(const FVector3d& positionM, const FVector3d& forwardDir) const;

    // Places the ship for view mView
    void SetupView();

    // Returns the current view's name (V1 .. V5, V4b) for the checks, logs and screenshot names
    const TCHAR* GetViewName() const;

    // Sets the pending HUD glow (never applied); nothing without a settings subsystem
    void SetPendingGlow(float hudGlow) const;

    // Checks the lane component, the points' crossfade flag and the pushed glow against the expected lanes state
    void CheckLaneState(const TCHAR* label, bool bExpectShown, float expectedHudGlow);

    // Checks every lane's group-texel .w values against a fresh histogram and BuildLaneFrame
    void CheckGroupTexels();

    // Logs each lane's endpoints on screen and the outbound line's side there (keep right, checked by eye)
    void LogLaneScreenSides(const ASOLShipPawn& pawn) const;

    // Requests a screenshot (no UI) named NpcLanes_<width>x<height>_<name>.png
    void TakeShot(const FString& name) const;

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    TWeakObjectPtr<ASOLNpcVisuals> mVisuals;
    TWeakObjectPtr<ASOLOrbitLineVisuals> mOrbitLines;
    TWeakObjectPtr<USOLNpcSubsystem> mNpcs;
    TWeakObjectPtr<USOLShipSubsystem> mShips;
    TWeakObjectPtr<USOLBodyRegistrySubsystem> mRegistry;
    TWeakObjectPtr<USOLSettingsSubsystem> mSettings;
    TWeakObjectPtr<USOLMenuSubsystem> mMenu;
    TWeakObjectPtr<USOLSimClockSubsystem> mSimClock;
    ESOLLanePhase mPhase = ESOLLanePhase::WaitPopulation;
    double mPhaseTimeS = 0.0;
    double mTotalTimeS = 0.0;
    int32 mPhaseFrames = 0;
    int32 mView = 0;                        // Current view (0-based: V1 = 0, V4b = 4, V5 = 5)
    int32 mVariant = 0;                     // Current variant of the view
    FVector3d mStartPositionM = FVector3d::ZeroVector;   // The ship's start (V1)
    double mFlowStartPhase = 0.0;           // Flow phase and platform time at a measurement's start
    double mFlowStartS = 0.0;
    int32 mOccluderWritesAtStart = 0;       // The orbit lines' lane-only occluder writes when the first view starts
    int32 mProfileView = 0;                 // Index into the profile views
    int32 mProfileFrame = 0;                // Frames into the running cost capture
    double mProfileStartS = 0.0;            // Script clock at the capture's start
    bool mIsProfile = false;                // -SOLSmokeNpcLanesProfile in a standalone -game run
    bool mIsTexelChecked = false;
    int32 mChecksRun = 0;
    int32 mChecksPassed = 0;
};
