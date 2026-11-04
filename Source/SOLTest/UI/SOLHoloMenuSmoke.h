/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Interaction/SOLActionTable.h"
#include "UI/SOLHoloFrameBuild.h"
#include "UI/SOLHoloFrameWidget.h"

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UObject/StrongObjectPtr.h"

class APlayerController;
class FModifierKeysState;
class UGameViewportClient;
class ASOLFlightHud;
class ASOLShipPawn;
class SSOLHoloFrame;
class SWidget;
class USOLHoloMenuSubsystem;
class USOLOverlayMenuWidget;
class USOLInteractionSubsystem;
class USOLSettingsSubsystem;

/**
 * Verification-only script of the hologram menu kit and menus (-SOLSmokeHoloMenu, SDD 42 section 12), on the smoke
 * settings slot. Part 42c checks: the menu subsystem and its material (strength and view size pushed), the three cue
 * assets (one request of each cue moves the request, accepted and played counters; a repeat inside the rate limit is
 * not accepted), the HUD's menu layer (exists with the menu material, empty and unstamped with no menu open, the HUD
 * layer still drawing), a frame preview (a USOLHoloFrameWidget placed on the viewport with an Overlay-Menu-like
 * layout: open cue, mid-open draw-in, the full frame once Open with its backing, the hover cue and mark, the reused
 * build equal to a fresh one, strength 0 drawn whole and flat at once, strength 2, the close) with screenshots at
 * strengths 1, 0 and 2, and the cursor's strength following HoloMenuEffects, not the HUD slider (D19). Part 42d adds
 * the radial menu (section 12 step 5 and its part of step 9) with real injected input on a body's icon: the open cue
 * and the rim arcs' draw-in mid-open, a drag across two sectors (two hover cues, the hover glow), a release on an
 * enabled action (the select cue, the close drawn from the cached state), strength 0 (whole at once, no glow, flat
 * arcs, the selection and the cues still work), strength 2, a cancel and a release on a disabled sector (no select
 * cue), and a release on Jump To (the select cue on the release frame, though the warp hides the HUD), with
 * screenshots mid-open, hovered, closing and at strengths 0 and 2. Part 42e adds the Ctrl+O Overlay Menu (section 12
 * steps 1-4, its part of step 9, and step 11) with real injected input: Ctrl+O (the open cue, the draw-in mid-way, then
 * the full outline, tab markers and dividers), the mouse onto a row and Down (hover cues, brackets and pointer), the
 * mouse onto a tab (brackets only), a row toggled and back, PageDown / PageUp and the cycle key (a select cue each),
 * Esc (input back in the same frame, the close plays, the widget gone after CLOSE_S), a reopen mid-close (reversed),
 * strength 0 (whole and flat at once, still usable, the cues still asked, closed at once) and 2, and the Overlay list
 * embedded in Settings (no frame, D17), with screenshots mid-open, hovered row and tab, closing and at strengths 0 and
 * 2. Part 42f adds the F3 speed panel (section 12 step 6 and its part of step 9) with injected F3, keys and a wheel
 * notch: the open cue, the draw-in mid-way, then the full outline with the stepped divider and the centre-tick mark on
 * the highlighted digit, a digit step (a select cue) and a highlight move (a hover cue), Tab and the wheel into the
 * list (hover cues, the row mark with its pointer), the wheel over the list (the row scrolled out of view: no mark, no
 * cue) and Enter (a select cue, the row's speed applied), Esc (input back in the same frame, the close plays), a
 * reopen mid-close (reversed), strength 0 (whole and flat at once, a digit step still works, the cues still asked,
 * closed at once) and 2, with screenshots mid-open, open, digit moved, row, row scrolled away, closing and at
 * strengths 0 and 2. The other menu checks of section 12 are stubs logged as "HoloMenu STUB" until their parts land.
 * Each check logs "HoloMenu CHECK <label> PASS|FAIL: <detail>", then a "HoloMenu SUMMARY" line; then it quits.
 * Screenshots go to Saved/Screenshots/<platform>/HoloMenu_<name>.png.
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

    // Returns whether a frame's (null: the preview's) last drawn frame equals a fresh BuildFrame of the same inputs
    bool IsFrameOutputFresh(const SSOLHoloFrame* frame = nullptr);

    // Counts a frame's (null: the preview's) last drawn shapes of one part
    int32 CountPart(ESOLHoloPart part, const SSOLHoloFrame* frame = nullptr) const;

    // Returns whether every line and dot of a frame's (null: the preview's) last frame has this glow pad
    bool AllLinePads(float padPx, const SSOLHoloFrame* frame = nullptr) const;

    // Returns the preview's Slate frame, or null
    const SSOLHoloFrame* GetPreviewFrame() const;

    // Returns the Overlay Menu's Slate frame, or null
    static SSOLHoloFrame* GetOverlayFrame(const ASOLShipPawn& pawn);

    // Returns how many outline pieces a frame draws at its full draw-in for its last pixel layout
    static int32 FullOutlineCount(const SSOLHoloFrame& frame);

    // Sends a key press and release through Slate (to the focused widget), with modifiers
    static void SlateKey(const FKey& key, const FModifierKeysState& modifiers);

    // Moves the mouse through Slate to a screen point (the OS cursor too)
    static void SlateMouseMove(const FVector2D& at);

    // Presses a key with a modifier held through the player controller (game input); both released next frame
    void TapGameChord(const FKey& modifier, const FKey& key);

    // Runs the Overlay Menu select step's timed key presses (row toggle and back, PageDown, PageUp, cycle key)
    void TickOverlaySelect(ASOLShipPawn& pawn, double elapsedS);

    // Runs a reverse step (the Overlay Menu's, or the F3 panel's when bIsSpeed): open, Esc once Open, the open key
    // mid-close, Esc once Open again
    bool TickReverse(ASOLShipPawn& pawn, bool bIsSpeed, double elapsedS);

    // Section 12 step 11: the Overlay list embedded in a Settings page has no frame widget and keeps its look
    void CheckEmbeddedOverlay();

    // Runs the F3 digit step's (Up, Left) or list step's (Tab, wheel, row mark read, Enter) presses; true once done
    bool TickSpeedKeys(ASOLShipPawn& pawn, bool bIsList, double elapsedS);

    // Returns the speed-panel binding's key from the applied settings (F3 by default)
    FKey GetSpeedPanelKey() const;

    // Returns the speed panel's Slate frame, or null
    static SSOLHoloFrame* GetSpeedFrame(const ASOLShipPawn& pawn);

    // Presses a game key through the player controller (game input); it is released next frame
    void TapGameKey(const FKey& key);

    // Turns the mouse wheel through Slate at the parked mouse point
    static void SlateWheel(ASOLShipPawn& pawn, float delta);

    // Turns the mouse wheel through Slate at a screen point
    static void SlateWheelAt(const FVector2D& at, float delta);

    // Moves the mouse to the right of the view, away from the Overlay Menu
    static void ParkMouse(ASOLShipPawn& pawn);

    // Returns the menu material's scalar parameter, or -1 when there is no material
    float GetMenuParam(const TCHAR* name) const;

    // Finds a body's icon for the radial steps (not the Sun, preferring one that is not the anchor); false if none
    bool FindRadialBody(ASOLShipPawn& pawn);

    // Presses Fire on the radial body's icon (opens the radial menu)
    void PressOnRadialBody(ASOLShipPawn& pawn) const;

    // Moves the cursor onto an action's sector of the open radial menu and updates the highlight; returns the sector
    // index, or INDEX_NONE when the menu does not offer the action
    int32 DragToRadialAction(ASOLShipPawn& pawn, ESOLAction action) const;

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
    TWeakObjectPtr<USOLInteractionSubsystem> mInteraction;
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
    int32 mSelectRequestsBefore = 0;
    int32 mExecutedBefore = 0;                              // Interaction counters at a step's start
    int32 mCancelsBefore = 0;
    FSOLObjectRef mRadialBody;                              // The body whose icon the radial steps press
    FVector2D mRadialBodyPx = FVector2D::ZeroVector;        // Its icon centre when found
    int32 mRadialSectorA = INDEX_NONE;                      // The two sectors the hover step crosses (Target, Info)
    int32 mRadialSectorB = INDEX_NONE;
    bool mIsSecondDrag = false;                             // The hover step's second drag happened
    float mMidCloseFraction = -1.0f;                        // Radial look caught mid-close: draw-in fraction,
    ESOLHoloMenuPhase mMidClosePhase = ESOLHoloMenuPhase::Closed;  // phase, sectors drawn and the cached highlight
    int32 mMidCloseSectors = 0;
    int32 mMidCloseHighlight = INDEX_NONE;
    int32 mMidCloseHovered = INDEX_NONE;                    // The anim's hovered sector mid-close (kept: glow fades)
    bool mIsMidCloseOpen = false;                          // The interaction still had the menu open mid-close
    float mFirstFlatFill = -1.0f;                           // Fill alpha of the first strength-0 radial frame
    int32 mFirstFlatSectors = 0;                            // Sectors it drew
    ESOLHoloMenuPhase mFlatClosePhase = ESOLHoloMenuPhase::Open;   // Phase the frame after a strength-0 release
    int32 mFlatCloseSectors = -1;                           // Sectors drawn then
    int32 mReleaseFrame = INDEX_NONE;                       // Disabled / Jump To steps: step frame of the release,
    double mReleaseS = 0.0;                                 // its step time,
    int32 mSelectsAtRelease = -1;                           // select cue requests the release frame asked,
    bool mIsWarpingAtRelease = false;                       // whether the warp ran then,
    float mReleaseRim = -1.0f;                              // the highlighted rim and the look just before it
    FSOLHoloMenuLook mReleaseLook;
    int32 mPaintsBefore = 0;                                // Preview paints / cache hits / cursor paints at a mark
    int32 mCacheHitsBefore = 0;
    int32 mCursorPaintsBefore = 0;
    float mMidOpenFraction = -1.0f;                         // Frame fraction caught mid-open
    float mFirstFlatFraction = -1.0f;                       // Frame fraction of the first strength-0 paint
    bool mIsMidOpenShot = false;
    TArray<FKey> mPendingReleases;                          // Game keys released at the next update
    double mOverlayOpenS = -1.0;                            // Step time the Overlay Menu opened, or not yet
    float mMidOpenOpacity = -1.0f;                          // Overlay content opacity caught mid-open
    int32 mSubStep = 0;                                     // A multi-action step's next action
    int32 mOverlayRow = INDEX_NONE;                         // The row the select step toggles
    FString mRowTextBefore;                                 // Its text before, after the first and the second toggle
    FString mRowTextToggled;
    int32 mViewedAfterPageDown = INDEX_NONE;                // Viewed profile after PageDown
    bool mIsInputBackAtEsc = false;                         // Esc: input back, closing, hit-test invisible, unfocused
    FString mEscDetail;
    bool mIsMidCloseInViewport = false;                     // Mid-close: still drawn, its opacity
    float mMidCloseOpacity = -1.0f;
    float mReverseProgress = -1.0f;                         // Reverse step: progress the reopen started from,
    ESOLHoloMenuPhase mReversePhase = ESOLHoloMenuPhase::Closed;   // the phase then,
    bool mIsReverseInViewport = false;                      // and the widget still on the viewport
    double mSubStepS = 0.0;                                 // Step time of the last sub-step action
    bool mIsFlatClosedAtOnce = false;                       // Strength-0 Esc: removed in the same frame
    double mSpeedCapRestoreMps = 0.0;                       // F3 steps: the cap before them (restored after),
    double mCapBeforeMps = -1.0;                            // the cap before a digit step and after it,
    double mCapAfterStepMps = -1.0;
    int32 mSpeedItemBefore = INDEX_NONE;                    // the highlighted item and list row at a step's start,
    int32 mSpeedListBefore = INDEX_NONE;
    int32 mSpeedRowHovers = -1;                             // and the list step's row mark: hover cue requests,
    int32 mSpeedRowPieces = -1;                             // hover pieces, dots and whether it is on the next row
    int32 mSpeedRowDots = -1;
    bool mIsSpeedRowItem = false;
    int32 mSpeedScrolledHovers = -1;                        // The list scrolled under the selected row: hover cue
    int32 mSpeedScrolledPieces = -1;                        // requests, hover pieces, dots, the scroll offset and
    int32 mSpeedScrolledDots = -1;                          // whether the selection and hovered item were kept
    float mSpeedScrollOffset = -1.0f;
    bool mIsSpeedScrolledItem = false;
    int32 mChecksPassed = 0;
    int32 mChecksRun = 0;
};
