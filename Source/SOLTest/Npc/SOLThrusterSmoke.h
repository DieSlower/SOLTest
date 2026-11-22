/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Visuals/SOLThrusterRibbons.h"

#include "CoreMinimal.h"

class ASOLNpcVisuals;
class ASOLOrbitLineVisuals;
class ASOLShipPawn;
class ASOLThrusterRibbons;
class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLNpcSubsystem;
class USOLShipSubsystem;
class UWorld;

/**
 * Verification-only NPC thruster plume script (-SOLSmokeThruster, SDD 51 part 51c). It requests a 1M population (unless
 * one is already requested), turns NPC combat (sol.Npc.Combat; not for the profile), the space dust, the orbit lines,
 * the lane lines and the tunnels off in memory, so no player setting is read or written. The viewpoint then rides
 * along (pinned every frame at a fixed offset, at the ship's own velocity) with the hardest-burning Earth-Mars lane ship
 * that stays in Accelerate or Brake, and later with one that stays in Coast or Flip at a Mid-tier, hull-drawn distance
 * when one exists (at the default radii the Full radius lies beyond the plume gate, so it is a Full ship). Over a window of frames every CPU ship is
 * scanned against what the renderer actually pushed (ASOLNpcVisuals::GetPushedPlume): P1 the hull-drawn lane ships in
 * Accelerate or Brake inside the plume gate with a plume of at least 1 reference px have b > 0 and L > 0, b near
 * SOLThruster::PlumeBrightness of the rail G (the worst error per AI tier reported; Full ships add their AI's
 * acceleration); P2 a coasting or flipping one has b = 0 (a Mid-tier one with b > 0 fails; Full ones are reported); P3
 * no dot-drawn ship has a plume or a plume thrust; P4 after sol.NpcPlumes 0 and back to 1 the burning ship's b is
 * nonzero again; P5 every hull-drawn ship's pushed floats (battle ships included, pool and instanced) equal
 * SOLThruster::PlumeInstance of its own plume thrust. The scans also run at the spawn view first (the battles' hulls
 * and the dot-drawn ships). One screenshot (Thruster_<w>x<h>_lane.png) frames the burning ship side-on. Each check
 * logs a LogSOL PASS / FAIL line, then a SUMMARY line, and the game quits.
 *
 * With -SOLSmokeThrusterProfile (-game only) it instead profiles sol.NpcPlumes 1 (A) against 0 (B, the plain hull
 * meshes, no custom floats and no Collect thrust) in interleaved windows (A B B A ...), first at the Earth spawn view
 * (the ship not moved) and then riding along with the hardest-braking Earth-Mars lane ship. Per window: a settle, then
 * `stat dumpave` of STAT_SOL_NpcRedraw over the window with three ProfileGPU frames, and the window means of the NPC
 * Collect pass, the whole NPC update, the GPU frame and the game thread logged as one PROFILE line.
 *
 * With -SOLSmokeThrusterRibbons (SDD 51 part 51e step 7, combat left on) it checks the thruster ribbons instead: a
 * screenshot at the Earth spawn view (the battles' turning ships), the frozen-frame no-pop pair there (time dilation at
 * its minimum and the ribbons' clock frozen; sol.ThrusterRibbonNiagaraPx at the preset's value twice, then huge twice,
 * so every NPC ribbon moves to the strip pool, with anti-aliasing off and then with the project's; then
 * sol.ThrusterRibbons 0 for the ribbon mask; without the HUD, the camera, anchor and trail counts logged and checked per
 * shot and the Niagara-drawn trails' screen places logged for the crops), the player thrusting while turning (its two
 * ribbons, side view), a ride with a lane ship in a U-turn and one with a burning lane ship at 22 km (the 1 px line).
 * With -SOLSmokeThrusterProfile too, the interleaved windows switch sol.ThrusterRibbons 1 (A) / 0 (B, the Niagara set
 * also deactivated) instead of sol.NpcPlumes, without ProfileGPU frames, and log the ribbons' own tick time and counts.
 */
class SOLTEST_API FSOLThrusterSmoke
{
public:

    // Caches the subsystems and actors, sets up the overrides and the population, and starts the first phase
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

private:

    // Script phases, in order (the profile flag goes from WaitPopulation to the Profile phases)
    enum class ESOLThrusterPhase : uint8
    {
        WaitPopulation,     // Until the population is uploaded (times out)
        SpawnScan,          // P1-P3 scanned every frame at the spawn view (the battles' CPU ships and dots)
        BurnSettle,         // Riding along with a burning lane ship; anchor and tiers settle
        BurnScan,           // P1-P3 scanned every frame
        PlumesOff,          // sol.NpcPlumes 0 for a few frames
        PlumesOn,           // sol.NpcPlumes 1 again; then P4
        Shot,               // The screenshot is written while still riding along
        CoastSettle,        // Riding along with a coasting lane ship at a Mid-tier hull distance
        CoastScan,          // P1-P3 scanned every frame; then the verdicts
        ProfileSpawn,       // Interleaved windows at the Earth spawn view
        ProfileLaneSettle,  // Riding along with a braking lane ship; settle
        ProfileLane,        // Interleaved windows in the lane
        RibbonSpawn,        // Ribbons: the spawn view settles; one screenshot
        RibbonPlayer,       // The player thrusts while turning (side view); one screenshot
        RibbonTurnSettle,   // Riding along with a lane ship in a U-turn; one screenshot
        RibbonPop,          // The frozen-frame no-pop shots (still riding the U-turn ship)
        RibbonFarSettle,    // Riding along with a burning lane ship at 22 km; one screenshot
        Done,
    };

    // Accumulated scan results (P1-P3) over the scan windows
    struct FScan
    {
        int32 Frames = 0;
        int32 Dots = 0;                     // Dot-drawn ship-frames seen
        int32 DotPlumes = 0;                // ...with pushed floats or a nonzero plume thrust
        int32 Hulls = 0;                    // Hull-drawn ship-frames seen
        int32 BurnSeen[2] = { 0, 0 };       // Per AI tier (0 Full, 1 Mid): Accelerate / Brake ship-frames checked
        int32 BurnLit[2] = { 0, 0 };        // ...with b > 0 and L > 0
        int32 BurnClose[2] = { 0, 0 };      // ...with b within the tier's tolerance of the rail G's brightness
        double BurnWorstErr[2] = { 0.0, 0.0 };
        int32 CoastSeen[2] = { 0, 0 };      // Coast / Flip ship-frames checked
        int32 CoastLit[2] = { 0, 0 };       // ...with b > 0
        double CoastMaxB[2] = { 0.0, 0.0 };
        int32 Turns = 0;                    // Skipped: in a U-turn
        int32 Transitions = 0;              // Skipped: the thrust state changes within a short step
        int32 Small = 0;                    // Skipped: the rail G's plume under 1 reference px
        int32 Unreadable = 0;               // Hull-drawn ships whose pushed floats could not be read
        int32 FloatChecked[2] = { 0, 0 };   // P5 per layer (0 pool, 1 instanced): hull ship-frames compared...
        int32 FloatLit[2] = { 0, 0 };       // ...of them with an expected plume...
        int32 FloatMismatch[2] = { 0, 0 };  // ...and with floats off the expected ones
        int32 FloatEdge = 0;                // Skipped: near the 0.5 px cut
        float FloatWorstB = 0.0f;
        float FloatWorstLengthM = 0.0f;
        TSet<int32> BurnShips;              // Distinct NPCs checked in Accelerate / Brake
        TSet<int32> CoastShips;             // Distinct NPCs checked in Coast / Flip
    };

    // Runs the actions of a phase's start (target picks, cvar switches, the screenshot)
    void EnterPhase(ESOLThrusterPhase phase);

    // Runs a phase's end checks
    void ExitPhase(ESOLThrusterPhase phase);

    // Returns the phase after this one
    ESOLThrusterPhase NextPhase(ESOLThrusterPhase phase) const;

    // Returns the minimum real duration (s) and frame count of a phase (0 for the frame-driven ones)
    static double GetPhaseDuration(ESOLThrusterPhase phase);
    static int32 GetPhaseFrames(ESOLThrusterPhase phase);

    // Logs the verdicts and the SUMMARY line, restores the cvars and takes the closing screenshot (which quits)
    void Finish(ASOLShipPawn& pawn);

    // Finds the Earth-Mars lane and its rail indices; false without the lane
    bool BuildLane();

    // Picks an Earth-Mars lane ship whose thrust state is one of the wanted ones (Coast also accepts Flip) now and
    // holdS later, off the U-turns: of the burns the hardest, else (and on ties) the one nearest to a fraction of the
    // lane (from Earth); INDEX_NONE if none
    int32 PickTarget(bool bBurn, bool bBrakeOnly, double holdS, double laneFraction) const;

    // Starts riding along with a lane ship at a distance on the lane's up side (the camera looking down at it)
    void StartRide(int32 npcIndex, double distanceM);

    // Pins the ship (each frame) at the ride's offset from the target, at the target's velocity, facing it
    void PinRide();

    // Returns the CPU ship index of an NPC this frame, or INDEX_NONE
    int32 FindCpuShip(int32 npcIndex) const;

    // Scans every CPU ship this frame for P1-P3
    void ScanFrame();

    // Runs one frame of an interleaved profile; returns true once every window is done
    bool TickProfile(const TCHAR* view, UWorld* world, float realDeltaSeconds);

    // Requests a screenshot (with or without the HUD) named Thruster_<width>x<height>_<name>.png
    void TakeShot(const TCHAR* name, bool bShowUi = true) const;

    // Runs one frame of the frozen-frame no-pop shots; returns true once every shot is written
    bool TickPop(UWorld* world);

    // Freezes (time dilation at its minimum, the ribbons' clock fixed) or thaws the game
    void SetFrozen(UWorld* world, bool bFrozen);

    // Logs the ribbons' counts, camera and anchor under a label, and the drawn NPC trails (all Niagara ones, the
    // longest pool ones and the longest at least farM away) with their screen places; returns the Niagara-drawn count
    int32 LogRibbons(const TCHAR* label, double farM) const;

    // Picks an Earth-Mars lane ship in a U-turn now and holdS later; INDEX_NONE if none
    int32 PickTurnTarget(double holdS) const;

    // Returns whether a phase ends with a ribbon screenshot
    static bool IsRibbonShotPhase(ESOLThrusterPhase phase);

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    TWeakObjectPtr<ASOLNpcVisuals> mVisuals;
    TWeakObjectPtr<ASOLOrbitLineVisuals> mOrbitLines;
    TWeakObjectPtr<USOLNpcSubsystem> mNpcs;
    TWeakObjectPtr<USOLShipSubsystem> mShips;
    TWeakObjectPtr<USOLBodyRegistrySubsystem> mRegistry;
    TWeakObjectPtr<USOLAnchorSubsystem> mAnchor;
    ESOLThrusterPhase mPhase = ESOLThrusterPhase::WaitPopulation;
    double mPhaseTimeS = 0.0;
    double mTotalTimeS = 0.0;
    int32 mPhaseFrames = 0;
    bool mIsProfile = false;                // -SOLSmokeThrusterProfile (-game only)
    int32 mLane = INDEX_NONE;               // The Earth-Mars lane
    TArray<int32> mLaneShips;               // Its rail indices

    // The ride-along
    int32 mTarget = INDEX_NONE;             // The NPC ridden along with (rail index)
    double mRideDistanceM = 0.0;
    FVector3d mRideOffsetDir = FVector3d::ZeroVector;   // Unit ecliptic offset from the target (the lane's up)
    FVector3d mRideUpDir = FVector3d::ZeroVector;       // Unit ecliptic screen up (the lane's right)
    FVector3d mTargetPrevM = FVector3d::ZeroVector;     // The target's position and rail time at the last pin
    double mTargetPrevRailS = 0.0;
    bool mHasTargetPrev = false;

    // P1-P4
    FScan mScan;
    bool mOffReadable = false;              // P4: the burning ship's floats readable with sol.NpcPlumes 0...
    float mOffBrightness = 0.0f;            // ...and their b
    int32 mBurnTarget = INDEX_NONE;         // P4: the burning ship (rail index)
    bool mHasBurnTarget = false;            // The burning ship was a CPU ship when the plumes went back on
    float mOnBrightness = 0.0f;             // ...and its b then
    float mOnLengthM = 0.0f;
    bool mIsMidBandEmpty = false;           // P2: no Mid-tier hull distance exists (Full radius beyond the hulls')

    // The profile
    int32 mProfileWindow = 0;               // Window in the current view
    int32 mProfileFrame = 0;                // Frame in the window
    double mSumCollectMs = 0.0;             // Window sums over the measured frames
    double mSumNpcMs = 0.0;
    double mSumGpuMs = 0.0;
    double mSumGameMs = 0.0;
    double mSumFrameMs = 0.0;
    int32 mSumFrames = 0;
    int32 mGpuFrames = 0;                   // Frames in the GPU mean (ProfileGPU frames left out)
    int32 mWindowHulls = 0;                 // Hull-drawn ships and lit plumes at the window's start
    int32 mWindowLit = 0;
    double mSumRibbonMs = 0.0;              // Ribbon mode: the ribbons' tick and trail counts, window sums
    double mSumRibbonPhaseMs[ASOLThrusterRibbons::RIBBON_PHASES] = {};
    double mSumCulledTrails = 0.0;
    double mSumNiagaraTrails = 0.0;
    double mSumPoolTrails = 0.0;
    double mSumLiveTrails = 0.0;

    // The ribbon checks
    TWeakObjectPtr<ASOLThrusterRibbons> mRibbons;
    bool mIsRibbons = false;                // -SOLSmokeThrusterRibbons
    int32 mRunNiagaraPx = -1;               // -SOLSmokeRibbonNiagaraPx= (sol.ThrusterRibbonNiagaraPx of the run)
    bool mIsShotTaken = false;              // The current phase's screenshot was requested
    double mShotHoldS = 0.0;                // Real time left for a requested screenshot to be written
    int32 mPopShot = 0;                     // Shot index and frame in the no-pop sequence
    int32 mPopFrame = 0;
    int32 mAntiAliasingDefault = INDEX_NONE;    // r.AntiAliasingMethod before the sequence
    FVector mPopCameraCm = FVector::ZeroVector; // The first shot's camera, anchor and counts (the invariants)
    FVector3d mPopAnchorCm = FVector3d::ZeroVector;
    int32 mPopLive = 0;
    int32 mPopNiagaraNpc = 0;
    int32 mPopPool = 0;
    bool mIsPopVacuous = true;              // No NPC trail was drawn by Niagara in the first shot
    bool mIsPopStable = true;               // Camera, anchor and live count equal in every shot
    bool mIsPopSwitched = true;             // Every pool shot: no NPC trail in Niagara, the pool grown by the switch

    int32 mChecksRun = 0;
    int32 mChecksPassed = 0;
};
