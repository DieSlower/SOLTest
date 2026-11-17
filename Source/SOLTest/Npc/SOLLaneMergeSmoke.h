/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

class ASOLFlightHud;
class ASOLNpcVisuals;
class ASOLOrbitLineVisuals;
class ASOLShipPawn;
class USOLBodyRegistrySubsystem;
class USOLLaneTrafficSubsystem;
class USOLNpcSubsystem;
class USOLShipSubsystem;
class USOLSimClockSubsystem;

/**
 * Verification-only lane traffic readout script (-SOLSmokeLaneMerge, SDD 50 section 8 item 2, V6). It requests an NPC
 * population (unless one is already requested), detaches the lanes, the tunnels and the readout from the Lanes overlay
 * row (verification overrides), turns the orbit lines and the space dust off in memory, so no player setting is read
 * or written, and lifts the window size limit for a -ResX / -ResY view. Once the lane table exists it places the ship
 * mid-lane on Earth - Mars at rho 1.5, 0.8 and 0.2 on the outbound half with 0.9, 1.0 and 1.3 x the traffic speed
 * along the lane (relative to the lane frame) and checks the HUD's readout: shown for that lane, TOO SLOW amber, MATCHED
 * green, TOO FAST red, drawn inside the view as its tunnel label's last line (inside that label's box) or, only when no
 * label of the lane is drawn, on the fallback line (never over a drawn label); then hidden at rho 2.5, on the axis a
 * quarter of the way from Earth (the -SOLSmokeNpcTunnel T2 view: the readout must be in its label) and one wall radius
 * off it looking 1.5 degrees outward (no label placed there: the fallback line), and the warp note at 1000x. Each
 * check logs a LogSOL PASS / FAIL line, then a SUMMARY line, and the game quits. Screenshots are
 * LaneMerge_<w>x<h>_<case>.png.
 */
class SOLTEST_API FSOLLaneMergeSmoke
{
public:

    // Caches the subsystems and actors, sets up the overrides, the view and the population
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

private:

    // One placement and what the readout must show there
    struct FMergeCase
    {
        const TCHAR* Name;          // Check and screenshot name
        double Rho;                 // Radius in wall radii, on the outbound (right) half
        double LegFraction;         // Where along the leg (from end ring A)
        double YawDeg;              // The nose turned this far from the axis toward Right
        double SpeedFactor;         // Along-lane speed relative to the lane frame, in traffic speeds
        bool bShown;                // The readout shows
        uint8 Verdict;              // ESOLLaneMergeVerdict when shown
        uint8 Band;                 // ESOLLaneMergeBand when shown
        bool bWarp;                 // Run at 1000x (the warp note)
        bool bShot;                 // Take a screenshot
        uint8 Where;                // ESOLLaneMergeWhere it must be drawn at in this view when shown (the pinned
                                    // date's views: Label, or Fallback where no label of its lane is placed), or
                                    // LANEMERGE_LABEL_OR_Q10 (the label, or the fallback line by Q10)
    };

    // Places the ship for a case: at its leg fraction and rho, moving along the lane at its speed factor
    void PlaceCase(const FMergeCase& mergeCase);

    // Checks the HUD's readout against a case and takes its screenshot
    void CheckCase(const FMergeCase& mergeCase);

    // Finds the Earth - Mars lane in the lane table; false without it
    bool FindLane();

    // Logs the SUMMARY line, restores the warp and the space dust and takes the closing screenshot (which quits)
    void Finish(ASOLShipPawn& pawn);

    // Requests a screenshot with the HUD named LaneMerge_<width>x<height>_<name>.png
    void TakeShot(const TCHAR* name) const;

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    TWeakObjectPtr<ASOLNpcVisuals> mVisuals;
    TWeakObjectPtr<ASOLOrbitLineVisuals> mOrbitLines;
    TWeakObjectPtr<ASOLFlightHud> mHud;
    TWeakObjectPtr<USOLNpcSubsystem> mNpcs;
    TWeakObjectPtr<USOLShipSubsystem> mShips;
    TWeakObjectPtr<USOLBodyRegistrySubsystem> mRegistry;
    TWeakObjectPtr<USOLLaneTrafficSubsystem> mLaneTraffic;
    TWeakObjectPtr<USOLSimClockSubsystem> mSimClock;
    bool mIsWaiting = true;                 // Waiting for the population and the lane table
    int32 mCase = 0;                        // The current case
    bool mIsPlaced = false;                 // The current case's ship is placed
    bool mIsChecked = false;                // ...and checked (its screenshot being written)
    double mPhaseTimeS = 0.0;
    double mTotalTimeS = 0.0;
    int32 mPhaseFrames = 0;
    int32 mLane = INDEX_NONE;               // The Earth - Mars lane
    int32 mLabelSpots = 0;                  // Shown cases drawn in the tunnel label / on the fallback line
    int32 mFallbackSpots = 0;
    int32 mSpotMisses = 0;                  // Shown cases drawn elsewhere than their view's expected spot
    int32 mNoFitFrames = 0;                 // Frames of this case's settle with the readout's grown label fitting
                                            // nowhere (Q10)
    FBox2D mLastNoFitLabelBox = FBox2D(ForceInit);  // Its lane's label box on the last of those frames
    int32 mChecksRun = 0;
    int32 mChecksPassed = 0;
};
