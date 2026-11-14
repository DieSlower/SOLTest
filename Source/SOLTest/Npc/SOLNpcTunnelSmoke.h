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
class APlayerController;
class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLMenuSubsystem;
class USOLNpcSubsystem;
class USOLSettingsSubsystem;
class USOLShipSubsystem;
struct FSOLLaneBasis;

/**
 * Verification-only NPC lane tunnel script (-SOLSmokeNpcTunnel, SDD 49 section 8 item 2). It requests a 1M population
 * (unless one is already requested), detaches the lanes from the Lanes overlay row and the orbit lines from their saved
 * switch (verification overrides, orbit lines off so the lanes and tunnels alone keep the shared occluders current),
 * turns the space dust off in memory and sets the HUD glow as a pending value only (reverted). Views T1 (75 lane radii
 * beside the Earth-Mars lane's middle), T2 / T3 (on its axis a quarter of the way from Earth looking toward Mars /
 * Earth), T4 (there looking 90 degrees off the axis), T4b (one tunnel radius off the axis looking 1.5 degrees off it),
 * T5 (the start above Earth facing the Moon) and T5b (40,000 km beyond Earth from Mars, 15 degrees off the Earth-Mars
 * axis, facing Earth) each check the instances, every texel against the tunnel feed, the fade read back (Q5) and its
 * crossfade, the labels (strongest first, side alpha, boxes clear of the projected dot rows and of each other), in
 * T2 / T3 keep right and the chevron directions, in T5b the end rings outside Earth and Earth among the shared
 * occluders hiding the tunnel parts behind it, and shoot NpcTunnel_<w>x<h>_<view>.png with the HUD.
 * T7 holds the ship at rest relative to the lane (phase unchanged), flies it along the axis and back at a fixed speed
 * (the phase follows the foot arc, the arc follows speed x time) and moves it from 50 to 1 tunnel radii toward the axis
 * (the lane-fixed rings stay put). Then the glow feed and its revert, the labels hidden with the pause menu, the Lanes
 * row off, sol.NpcTunnel 0 and the jump map (tunnels hidden, every fade texel 0), T6 (Demo lane fraction 0: empty
 * lanes keep minimum-radius tunnels, no lane line, the shared occluders still written) and the hidden override. Each
 * check logs a LogSOL PASS / FAIL line, then a SUMMARY line, and the game quits. With -SOLSmokeNpcTunnelProfile (-game
 * only) it skips the views and checks after the population and runs the cost captures instead (SDD 49 section 12c):
 * per pass (T2 and T5 with every lane forced in range, T2 with its own lanes only) a settle, then with the tunnels
 * drawn and with sol.NpcTunnel 0 a stat dumpave of STAT_SOL_NpcRedraw over 240 frames with three ProfileGPU frames
 * inside it, a stat dumpave of STAT_SOL_HudLaneLabels over 240 frames and a screenshot.
 */
class SOLTEST_API FSOLNpcTunnelSmoke
{
public:

    // Caches the subsystems and actors, sets up the overrides and the population, and starts the first phase
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

private:

    // Script phases, in order (ViewSetup / ViewCheck repeat per view)
    enum class ESOLTunnelPhase : uint8
    {
        WaitPopulation,     // Until the population is uploaded and a tunnel draws (times out)
        ViewSetup,          // The ship moves to the next view and settles
        ViewCheck,          // ...the view's checks and its screenshot
        RestHold,           // T7: the ship held at a fixed point relative to the lane
        FlyForward,         // ...flown along the axis toward B at a fixed speed
        FlyBack,            // ...and back toward A
        Approach,           // ...from 50 to 1 tunnel radii toward the axis
        GlowSet,            // The pending HUD glow at 200%: the tunnel material follows
        GlowRevert,         // ...and returns to the saved glow
        PauseOpen,          // The pause menu: the labels hide with the HUD
        PauseClose,
        HudHide,            // The flight HUD hidden (HUD-visible setting, pending only): no label drawn
        HudShow,
        LanesOff,           // The Lanes row off (lane override): tunnels hidden, fades 0
        LanesOn,
        CvarOff,            // sol.NpcTunnel 0: tunnels hidden, fades 0
        CvarOn,
        MapOpen,            // The jump map: tunnels hidden, fades 0 (screenshot)
        MapClose,
        Repopulate,         // Demo lane fraction 0: every lane empty (waits for the new population)
        EmptySetup,         // T6: the ship on the Earth-Mars axis again
        EmptyCheck,         // ...minimum-radius tunnels, no lane line, occluders still written (screenshot)
        OverrideHidden,     // The hidden tunnel override: tunnels hidden, fades 0
        ProfileSetup,       // Profile flag: the pass's view, lanes forced or not, settles
        ProfileOn,          // ...the captures with the tunnels drawn
        ProfileOff,         // ...and with sol.NpcTunnel 0
        Done,
    };

    // Runs a phase's entry actions
    void EnterPhase(ESOLTunnelPhase phase, ASOLShipPawn& pawn);

    // Runs a phase's per-frame actions (the flights)
    void TickPhase(ESOLTunnelPhase phase);

    // Runs a phase's exit checks
    void ExitPhase(ESOLTunnelPhase phase, ASOLShipPawn& pawn);

    // Returns the next phase after one has ended
    ESOLTunnelPhase NextPhase(ESOLTunnelPhase phase);

    // Returns the minimum real duration of a phase in seconds
    static double GetPhaseDuration(ESOLTunnelPhase phase);

    // Logs the SUMMARY line and takes the closing screenshot (which quits)
    void Finish(ASOLShipPawn& pawn);

    // Finds the Earth-Mars lane in the lane frame; false without it
    bool FindLane(int32& outLane, FSOLLaneBasis& outBasis, double& outLengthM, double& outTunnelRadiusM) const;

    // Places the ship (universe ecliptic metres) facing a direction, at the reference velocity, not rotating
    void SetupShip(const FVector3d& positionM, const FVector3d& forward) const;

    // Places the ship for view mView (T1 .. T5b, T4b; T6 = the T2 point)
    void SetupView();

    // Returns the current view's name for the checks, logs and screenshot names
    const TCHAR* GetViewName() const;

    // Checks the instances, the texels against the feed, the fade read back and its crossfade
    void CheckFeed(const TCHAR* label);

    // Checks the label snapshot's order and alphas and the drawn boxes against the dot rows and each other
    void CheckLabels(const TCHAR* label);

    // Checks keep right and the chevron directions on screen (T2 looking toward B, T3 toward A)
    void CheckSides(const TCHAR* label, bool bLookingTowardB);

    // Checks T5b: the end rings at Earth outside the planet, Earth among the shared occluders and hiding part of the
    // tunnels behind it
    void CheckEarthEnds(const TCHAR* label);

    // Checks that the tunnels are hidden and every fade texel is 0
    void CheckHidden(const TCHAR* label);

    // Returns the Earth-Mars lane's ring phase and foot arc from the component's feed; false when it is not shown
    bool ReadLanePhase(double& outPhase, double& outFootArcM, double& outRadialM) const;

    // Sets the pending HUD glow (never applied)
    void SetPendingGlow(float hudGlow) const;

    // Requests a screenshot with the HUD named NpcTunnel_<width>x<height>_<name>.png
    void TakeShot(const FString& name) const;

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    // Runs one profile capture from its first frame (the dumps, ProfileGPU, the screenshot); true once it is done
    bool TickProfile(const TCHAR* label, UWorld* world);

    TWeakObjectPtr<ASOLNpcVisuals> mVisuals;
    TWeakObjectPtr<ASOLOrbitLineVisuals> mOrbitLines;
    TWeakObjectPtr<ASOLFlightHud> mHud;
    TWeakObjectPtr<USOLNpcSubsystem> mNpcs;
    TWeakObjectPtr<USOLShipSubsystem> mShips;
    TWeakObjectPtr<USOLBodyRegistrySubsystem> mRegistry;
    TWeakObjectPtr<USOLAnchorSubsystem> mAnchor;
    TWeakObjectPtr<USOLSettingsSubsystem> mSettings;
    TWeakObjectPtr<USOLMenuSubsystem> mMenu;
    TWeakObjectPtr<APlayerController> mController;
    ESOLTunnelPhase mPhase = ESOLTunnelPhase::WaitPopulation;
    double mPhaseTimeS = 0.0;
    double mTotalTimeS = 0.0;
    int32 mPhaseFrames = 0;
    int32 mView = 0;                        // Current view (T1 = 0 .. T5 = 5, T5b = 6, T6 = 7)
    FVector3d mStartPositionM = FVector3d::ZeroVector;  // The ship's start (T5), ecliptic
    double mLaneParam = 0.25;               // T7: the lane point's parameter the ship is held at
    double mStartPhase = 0.0;               // T7: phase, foot arc and unwrapped phase advance at a phase's start
    double mStartFootArcM = 0.0;
    double mPrevPhase = 0.0;
    double mUnwrappedPhase = 0.0;
    double mApproachMaxStepM = 0.0;         // T7 approach: the largest per-frame move of the lane-fixed ring
    double mCaptureAxialM = 0.0;            // T7: the axial offset set at the measurement's start and last frame
    double mLastSetAxialM = 0.0;
    double mCaptureTimeS = 0.0;             // T7: the part's clock at the measurement's start
    int32 mOccluderWritesAtStart = 0;       // T5b, T6: the orbit lines' lane-only occluder writes at the view's start
    uint32 mPopulationRevision = 0;         // Repopulate: the revision before the lane fraction changed
    int32 mChecksRun = 0;
    int32 mChecksPassed = 0;
    bool mIsProfile = false;                // -SOLSmokeNpcTunnelProfile in a standalone -game process
    int32 mProfilePass = 0;                 // Current profile pass
    int32 mProfileFrame = 0;                // Frames into the current capture
    bool mIsProfileDone = false;            // The current capture has finished
};
