/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "UI/SOLHoloFrameBuild.h"
#include "UI/SOLHoloFrameWidget.h"

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class APlayerController;
class UGameViewportClient;
class ASOLFlightHud;
class ASOLShipPawn;
class SWidget;
class USOLHoloMenuSubsystem;
class USOLSettingsSubsystem;

/**
 * Verification-only script of the hologram menu kit and menus (-SOLSmokeHoloMenu, SDD 42 section 12), on the smoke
 * settings slot. Part 42c checks: the menu subsystem and its material (strength and view size pushed), the three cue
 * assets (one request of each cue moves the request, accepted and played counters; a repeat inside the rate limit is
 * not accepted), the HUD's menu layer (exists with the menu material, empty and unstamped with no menu open, the HUD
 * layer still drawing), a frame preview (a USOLHoloFrameWidget placed on the viewport with an Overlay-Menu-like
 * layout: open cue, mid-open draw-in, the full frame once Open with its backing, the hover cue and mark, the reused
 * build equal to a fresh one, strength 0 drawn whole and flat at once, strength 2, the close) with screenshots at
 * strengths 1, 0 and 2, and the cursor's strength following HoloMenuEffects, not the HUD slider (D19). The menu checks
 * of section 12 (steps 1-9 and 11) are stubs logged as "HoloMenu STUB" until their parts land. Each check logs
 * "HoloMenu CHECK <label> PASS|FAIL: <detail>", then a "HoloMenu SUMMARY" line; then it quits. Screenshots go to
 * Saved/Screenshots/<platform>/HoloMenu_<name>.png.
 */
class SOLTEST_API FSOLHoloMenuSmoke
{
public:

    // Removes the frame preview if it is still on the viewport
    ~FSOLHoloMenuSmoke();

    // Caches the controller, HUD and subsystems and starts the first step
    void Start(ASOLShipPawn& pawn);

    // Runs one frame of the script (from the pawn's tick); returns true once it has finished
    bool Update(ASOLShipPawn& pawn, float deltaSeconds);

private:

    // Runs the step's action at its start
    void EnterStep(int32 step, ASOLShipPawn& pawn);

    // Runs the step's per-frame work; returns true once the step is done
    bool TickStep(int32 step, ASOLShipPawn& pawn);

    // Runs the step's checks at its end
    void ExitStep(int32 step, ASOLShipPawn& pawn);

    // Once ready, requests the step's screenshot and returns true a few frames later (the step is done)
    bool ShotThenDone(const TCHAR* name, bool bIsReady);

    // Sets the live (pending, never saved) HoloMenuEffects, as the Settings slider previews it
    void SetStrength(float strength) const;

    // Creates the frame preview widget, sets its layout and puts it on the viewport
    void AddFramePreview(ASOLShipPawn& pawn);

    // Takes the frame preview off the viewport
    void RemoveFramePreview();

    // Returns whether the preview's last drawn frame equals a fresh BuildFrame of the same inputs (the reuse path)
    bool IsFrameOutputFresh();

    // Counts the preview's last drawn shapes of one part
    int32 CountPart(ESOLHoloPart part) const;

    // Returns whether every line and dot of the preview's last frame has this glow pad
    bool AllLinePads(float padPx) const;

    // Returns the menu material's scalar parameter, or -1 when there is no material
    float GetMenuParam(const TCHAR* name) const;

    // Logs the section 12 checks later parts fill in
    static void LogStubs();

    // Requests a window screenshot with the UI to the screenshot folder (Saved/Screenshots/<platform>)
    static void TakeUiShot(const TCHAR* name);

    // Logs one check with its verdict and counts it
    void Check(const TCHAR* label, bool bPassed, const FString& detail);

    TWeakObjectPtr<APlayerController> mController;
    TWeakObjectPtr<ASOLFlightHud> mHud;
    TWeakObjectPtr<USOLHoloMenuSubsystem> mHoloMenu;
    TWeakObjectPtr<USOLSettingsSubsystem> mSettings;
    TStrongObjectPtr<USOLHoloFrameWidget> mFrameWidget;     // The frame preview (kept alive by the script)
    TSharedPtr<SWidget> mPreviewRoot;                       // Its viewport content (placement box)
    TWeakObjectPtr<UGameViewportClient> mPreviewViewport;   // The viewport it was added to (outlives the controller)
    FSOLHoloFrameOutput mFreshOutput;                       // A fresh build to compare with (8 KB: a member)
    double mStepStartS = 0.0;                               // Real time the step started
    int32 mStep = 0;
    int32 mStepFrames = 0;
    int32 mShotFrame = INDEX_NONE;                          // Step frame of the step's screenshot request, or none
    int32 mOpenRequestsBefore = 0;                          // Cue counters at a step's start
    int32 mHoverRequestsBefore = 0;
    int32 mPaintsBefore = 0;                                // Preview paints / cache hits / cursor paints at a mark
    int32 mCacheHitsBefore = 0;
    int32 mCursorPaintsBefore = 0;
    float mMidOpenFraction = -1.0f;                         // Frame fraction caught mid-open
    float mFirstFlatFraction = -1.0f;                       // Frame fraction of the first strength-0 paint
    bool mIsMidOpenShot = false;
    int32 mChecksPassed = 0;
    int32 mChecksRun = 0;
};
