/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Interaction/SOLActionTable.h"
#include "Targeting/SOLTargetable.h"

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/StrongObjectPtr.h"

#include "SOLChaseSmoke.generated.h"

class APlayerController;
class ASOLFlightHud;
class ASOLShipPawn;
class USOLCombatSubsystem;
class USOLInteractionSubsystem;
class USOLShipSubsystem;

/**
 * Verification-only stand-in for a large chase target (-SOLSmokeChase, SDD 32 section 5): a dropped target's slot
 * seen with a much larger radius, so the size-scaled standoff (3 x radius) can be checked without a big real target.
 * Never registered with targeting.
 */
UCLASS()
class SOLTEST_API USOLChaseSmokeTarget : public UObject, public ISOLTargetable
{
    GENERATED_BODY()

public:

    // Points the stand-in at a combat slot and the drop it holds now, with the radius it reports
    void Bind(USOLCombatSubsystem* combat, int32 slot, uint32 spawnSerial, double radiusM);

    // Returns the slot's live position and velocity (Unreal-handed universe frame) with the stand-in radius
    virtual FSOLTargetInfo GetTargetInfo() const override;

    // Returns true while the bound slot still holds the same drop
    virtual bool IsTargetAlive() const override;

    // Returns the combat frame body the drop is held relative to
    virtual int32 GetTargetFrameBodyIndex() const override;

private:

    TWeakObjectPtr<USOLCombatSubsystem> mCombat;
    mutable FSOLTargetInfo mLastInfo;               // Last live snapshot, returned once the drop is gone
    double mRadiusM = 0.0;
    int32 mSlot = INDEX_NONE;
    uint32 mSpawnSerial = 0;
};

/**
 * Verification-only Chase Down run (-SOLSmokeChase, SDD 32 section 5), driven through the pawn's real paths: a target
 * dropped and the ship moved 20 km away, Chase Down from the radial menu (scripted flight cursor, as the Interact
 * smoke), AUTO, a burst within 2 s, intercept within the prediction + 25%, 10 s on the tail, cancels by W, Esc (a
 * second Esc opens the pause menu), Move To and C (a Moon chase), the selection kept, a 30% margin, the size-scaled
 * standoff, 100x and 1000x warp, the boost toggle off and on, and the target destroyed (target-lost note). The meter is
 * checked against the floor every frame of every chase. Screenshots with the UI go to Saved/Screenshots/ChaseSmoke_*.png.
 * With -SOLSmokeChaseProfile it also captures the SOL ship cycle stats during the 20 km chase, the Moon chase, a 1 d/s
 * warp Moon chase and a 1 d/s Move To (stat dumpsum / dumpmax in the log).
 * Runs with the smoke settings slot and quits at the end. Each check is logged as
 * "SmokeChase CHECK <label> PASS|FAIL: <detail>".
 */
class SOLTEST_API FSOLChaseSmoke
{
public:

    // Caches the controller, HUD and subsystems and starts the first step
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

private:

    // Per-chase measurements, reset whenever a chase is engaged
    struct FChaseRun
    {
        double EngagedAtS = 0.0;            // Step clock time of the engage
        double EntryMeter = 1.0;            // Meter at the engage
        double PredictedS = 0.0;            // Plan prediction at the engage
        double MinMeter = 1.0;
        double MaxLoadG = 0.0;
        double MaxLimitG = 0.0;
        double FirstBurstS = -1.0;          // Since the engage: first load above sustained
        double InterceptS = -1.0;           // Since the engage: first Hold
        double MaxTargetDistanceM = 0.0;    // While measured (see bMeasureDistance)
        int32 Frames = 0;
        uint32 ArrivalsAtEngage = 0;
        bool bMeasureDistance = false;
    };

    // Runs the step's action at its start
    void EnterStep(int32 step, ASOLShipPawn& pawn);

    // Runs the step's per-frame work; returns true once the step is done
    bool TickStep(int32 step, ASOLShipPawn& pawn);

    // Runs the step's checks at its end
    void ExitStep(int32 step, ASOLShipPawn& pawn);

    // Watches the running chase every frame: the meter floor, the peaks, the intercept and the screenshots
    void MonitorChase(ASOLShipPawn& pawn);

    // Resets the per-chase measurements at an engage
    void BeginRun();

    // Engages Chase Down on the dropped target through the interaction subsystem (the menus' Execute path)
    bool ChaseTarget();

    // Moves the ship (attitude kept) to a distance behind the dropped target along its own forward axis, so the target
    // is straight ahead, flying away from it at recedeMps in the reference frame
    void PlaceShipBehindTarget(double distanceM, double recedeMps) const;

    // Wakes the pilot with a full meter (ends any order; the pawn turns boost off) and moves the ship 20 km away, flying
    // away at recedeMps
    void ResetAndPlace(double recedeMps) const;

    // Wakes the pilot with a full meter and moves the ship, at rest in the Moon's frame, just outside the Moon chase's
    // tail point on the side it is on now
    void PlaceShipNearMoon() const;

    // Turns the Tab boost toggle to the wanted state through the pawn
    static void SetBoost(ASOLShipPawn& pawn, bool bOn);

    // Applies a Chase Down safety margin through the settings (the smoke settings slot)
    void ApplyMargin(float margin) const;

    // Fires bolts from the ship at the dropped target
    void FireAtTarget() const;

    // Returns the current screen centre of the dropped target's icon, false when it is not drawn
    bool FindTargetIcon(FVector2D& outCenterPx) const;

    // Moves the cursor onto an action's sector of the open radial menu; false when the menu does not offer it
    bool DragToAction(ASOLShipPawn& pawn, ESOLAction action) const;

    // Requests a window screenshot with the UI (at most one per frame)
    void TakeUiShot(const TCHAR* name);

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    // With -SOLSmokeChaseProfile: runs one cost capture (stat dumpsum, then stat dumpmax, of the SOL ship stats) from
    // the first frame it is called; returns true once both dumps are done, or at once without the flag
    bool TickProfile(const TCHAR* label, UWorld* world);

    TWeakObjectPtr<APlayerController> mController;
    TWeakObjectPtr<ASOLFlightHud> mHud;
    TWeakObjectPtr<USOLInteractionSubsystem> mInteraction;
    TWeakObjectPtr<USOLShipSubsystem> mShips;
    TWeakObjectPtr<USOLCombatSubsystem> mCombat;
    TStrongObjectPtr<USOLChaseSmokeTarget> mBigTarget;     // The size-scaled standoff stand-in
    FSOLObjectRef mTarget;                                  // The dropped target
    FSOLObjectRef mMoon;
    FChaseRun mRun;
    double mSustainedG = 1.5;
    double mUnboostedCapG = 3.0;
    double mStepTimeS = 0.0;
    double mClockS = 0.0;                                   // Real seconds since the script started
    double mFrameDeltaS = 0.0;                              // This frame's real delta
    double mHoldStreakS = 0.0;                              // Real seconds the running chase has held on the tail
    double mWorstFloorDeficit = 0.0;                        // Largest meter dip under the allowed floor, any chase
    double mLastLogS = 0.0;
    uint64 mLastShotFrame = 0;
    int32 mStep = 0;
    int32 mStepFrames = 0;
    int32 mSelectedAtStart = INDEX_NONE;                    // Targeting selection before the first chase
    int32 mDestroyedBefore = 0;
    int32 mFloorFrames = 0;                                 // Frames the meter was checked against the floor
    int32 mFloorViolations = 0;
    int32 mMoonFrames = 0;                                  // Moon warp chase: frames measured, frames reading HOLD
    int32 mMoonHoldFrames = 0;
    double mMoonMaxAimM = 0.0;                              // ... and the largest distance to the tail point
    int32 mChecksPassed = 0;
    int32 mChecksRun = 0;
    int32 mProfileFrame = 0;                                // Frames into this step's cost capture
    double mProfileStartS = 0.0;                            // Script clock at the capture's start
    bool mProfile = false;                                  // -SOLSmokeChaseProfile
    bool mHasShotBurst = false;
    bool mHasShotCool = false;
    bool mHasShotHold = false;
};
