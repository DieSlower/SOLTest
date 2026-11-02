/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Interaction/SOLActionTable.h"

#include "CoreMinimal.h"

class APlayerController;
class ASOLFlightHud;
class ASOLShipPawn;
class USOLInteractionSubsystem;

/**
 * Verification-only interaction menu run (-SOLSmokeInteract, SDD 22 section 2.4), driven through the pawn's real
 * paths with a scripted flight cursor: the HUD's icons become pointer hits, hovering a body icon highlights it, a Fire
 * press on it opens the radial menu (the trigger stays released), a drag-release on the Target sector targets it,
 * release in the centre and Esc cancel, Fire away from icons still fires, Info shows the card, Move To and Follow
 * engage the autopilot, Match speed locks the frame, Attack on a dropped target targets it with the hint, Jump To
 * jumps next to the body, and on the map a right-click list opens, takes Down / Esc and runs Move To. Screenshots
 * with the UI go to Saved/Screenshots/InteractSmoke_*.png (the software cursor is not captured). Runs with the smoke
 * settings slot and quits at the end. Each check is logged as "SmokeInteract CHECK <label> PASS|FAIL: <detail>".
 */
class SOLTEST_API FSOLInteractSmoke
{
public:

    // Caches the controller, HUD and interaction subsystem and starts the first step
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float realDeltaSeconds);

private:

    // Runs the step's action at its start
    void EnterStep(int32 step, ASOLShipPawn& pawn);

    // Runs the step's checks at its end
    void ExitStep(int32 step, ASOLShipPawn& pawn);

    // Finds an icon hit of an object kind (bodies: not the Sun, preferring one that is not the anchor); false if none
    bool FindHit(ESOLObjectKind kind, int32 anchorBody, FSOLObjectRef& outObject, FVector2D& outCenterPx) const;

    // Returns the current screen centre of the chosen object's icon (the last one found when it is not drawn now)
    FVector2D GetObjectIconPx(const FSOLObjectRef& object) const;

    // Presses Fire on the object's icon (opens the radial menu)
    void PressOnIcon(ASOLShipPawn& pawn, const FSOLObjectRef& object);

    // Moves the cursor onto an action's sector of the open radial menu; false when the menu does not offer it
    bool DragToAction(ASOLShipPawn& pawn, ESOLAction action);

    // Sends a key down and up through Slate (the list menu has the focus)
    static void SlateKey(const FKey& key);

    // Requests a window screenshot with the UI
    static void TakeUiShot(const TCHAR* name);

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    TWeakObjectPtr<APlayerController> mController;
    TWeakObjectPtr<ASOLFlightHud> mHud;
    TWeakObjectPtr<USOLInteractionSubsystem> mInteraction;
    FSOLObjectRef mBody;                                    // The body icon the script works on
    FSOLObjectRef mTarget;                                  // The dropped target's icon
    FVector2D mBodyPx = FVector2D::ZeroVector;              // Its icon centre when found
    FVector2D mTargetPx = FVector2D::ZeroVector;
    int32 mStep = 0;
    double mStepTimeS = 0.0;
    int32 mStepFrames = 0;
    int32 mExecutedBefore = 0;                              // Interaction counters at a step's start
    int32 mCancelsBefore = 0;
    int32 mJumpsBefore = 0;
    int32 mChecksPassed = 0;
    int32 mChecksRun = 0;
};
