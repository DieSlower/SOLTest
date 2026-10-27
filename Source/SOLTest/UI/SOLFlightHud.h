/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Flight/SOLFlight.h"
#include "Flight/SOLFlightMode.h"
#include "Map/SOLMapBodyOverlay.h"
#include "Map/SOLMapPickOverlay.h"
#include "UI/SOLHudFormat.h"
#include "UI/SOLRadarLayout.h"

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"

#include "SOLFlightHud.generated.h"

class ASOLOrbitLineVisuals;
class ASOLShipPawn;
class UFont;
class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLJumpSubsystem;
class USOLSettingsSubsystem;
class USOLMapModeSubsystem;
class USOLShipSubsystem;
class USOLSimClockSubsystem;
class USOLTargetingSubsystem;

/**
 * Sim-style flight HUD (SDD 2, sub-part 1c), drawn every frame on the HUD Canvas with no editor assets: reticle and
 * virtual-joystick indicator, speed relative to the active frame (absolute beneath), speed cap and fill bar, assist
 * and boost state, frame and anchor names, nearest body and altitude, the surface-lock status (or the L hint while a
 * body is within manual range; SDD 4 decision 6), sim UTC date and warp, the selected target's
 * bracket (or an edge arrow when off-screen), prograde/retrograde markers, a 3D spherical radar (AUTO range = the
 * target-dependent floor, or MANUAL decade zoom with '-' / '=' and Home back to AUTO) and, toggled by O, every body's
 * orbit ellipse. While the jump map is open the pilot aids are hidden, the hint line shows the map's keys and every
 * body too small on screen gets a colored icon and name label (FSOLMapBodyOverlay), with the destination pick's disc,
 * height guide line and marker drawn over them (FSOLMapPickOverlay); the orbit ellipses and target
 * bracket project through whichever camera is active. Every number is read from the universe, ship and targeting
 * subsystems. The draw path reuses member buffers (text, radar contacts, orbit points) and keeps one cached FText per
 * text line, rebuilt only when that line's text changes; what still allocates is the FText of a changed line and
 * whatever Canvas does internally to draw text.
 *
 * Settings (SDD 8, 8d): the player's HUD scale multiplies the canvas-height scale of every element, and with the HUD
 * hidden nothing of the flight HUD is drawn in flight (the warp streaks still are, and with the jump map open the
 * HUD is drawn as usual, since picking a destination needs its overlays).
 *
 * During a jump's warp sequence (SDD 3 Appendix I) the whole flight HUD is hidden and only the warp's radial streaks
 * are drawn: a fixed table of canvas lines from the screen center, built once, that stream outward and brighten and
 * lengthen with the sequence's streak intensity.
 */
UCLASS()
class SOLTEST_API ASOLFlightHud : public AHUD
{
    GENERATED_BODY()

public:

    // Disables ticking (all work happens in DrawHUD)
    ASOLFlightHud();

    // Draws every HUD element for this frame
    virtual void DrawHUD() override;

    // Toggles the world-space orbit lines (O), owned by ASOLOrbitLineVisuals (SDD 17)
    void ToggleOrbitLines();

    // Returns true while the world-space orbit lines are shown
    bool AreOrbitLinesVisible() const;

    // Returns the game mode's orbit-line actor, or nullptr
    ASOLOrbitLineVisuals* GetOrbitLineVisuals() const;

    // Returns the number of radar contacts plotted in the last frame (verification)
    int32 GetLastRadarContactCount() const { return mRadarContacts.Num(); }

    // Returns the radar range used in the last frame, meters (verification)
    double GetLastRadarRangeM() const { return mLastRadarRangeM; }

    // Steps the radar range one decade in ('-') or out ('='); switches to MANUAL only when the range changes
    void StepRadarZoom(bool bZoomIn);

    // Returns the radar to AUTO range (Home)
    void ResetRadarAuto();

    // Returns true while the radar range is set manually (verification)
    bool IsRadarManual() const { return mIsRadarManual; }

    // Returns true while the jump map is open (the view is the map camera, not the ship's)
    bool IsMapOpen() const;

    // Returns the jump map's body icon and label overlay (icon alphas, pick radii, draw counts)
    const FSOLMapBodyOverlay& GetMapBodyOverlay() const { return mMapBodyOverlay; }

    // Returns the jump map's destination-pick overlay (disc, guide line, marker draw state)
    const FSOLMapPickOverlay& GetMapPickOverlay() const { return mMapPickOverlay; }

    // Returns the flight key hint line as drawn with the orbit lines off (verification)
    const FString& GetFlightKeyHints() const { return mFlightHintsOrbitsOff; }

    // Returns the HUD texts that name a key, as drawn (verification)
    const SOLHudFormat::FHudKeyTexts& GetKeyTexts() const { return mKeyTexts; }

    // Returns the layout and font scale of the last frame (canvas height times the settings' HUD scale; verification)
    float GetUiScale() const { return mUiScale; }

    // Returns true if the last frame drew the flight HUD (false while a jump warp hides it; verification)
    bool WasFlightHudDrawn() const { return mWasFlightHudDrawn; }

    // Returns the number of warp streaks drawn in the last frame (verification)
    int32 GetLastWarpStreaksDrawn() const { return mLastWarpStreaksDrawn; }

    // Which surface-lock line the HUD drew: none, the L hint, the engaged status, or the status in the warning color
    enum class ESOLSurfaceLockHudLine : uint8
    {
        None,
        Hint,
        Engaged,
        Warning,
    };

    // Returns the surface-lock line drawn in the last frame (verification)
    ESOLSurfaceLockHudLine GetLastSurfaceLockLine() const { return mLastSurfaceLockLine; }

protected:

    // Caches the subsystems and sizes the per-body orbit point buffers once
    virtual void BeginPlay() override;

    // Unhooks from the settings' key-binding changes
    virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

private:

    // The camera as the HUD projects with it: location, rotation, focal length in pixels and the screen center
    struct FSOLHudView
    {
        FVector Location = FVector::ZeroVector;
        FQuat Rotation = FQuat::Identity;
        FVector2D Center = FVector2D::ZeroVector;
        double FocalPx = 1.0;
    };

    // Fills the view from the player camera's cached POV for this frame; false without a camera
    bool BuildView(FSOLHudView& outView) const;

    // Projects a camera-local point (X forward) to the screen; false when it is behind the near plane
    static bool ProjectLocal(const FSOLHudView& view, const FVector& localCm, FVector2D& outScreen);

    // Returns true when a screen point lies inside the canvas, shrunk by a margin
    bool IsOnScreen(const FVector2D& screen, float marginPx) const;

    // Draws a circle (or an ellipse when squashY < 1) as line segments
    void DrawCircle(const FVector2D& center, float radiusPx, const FLinearColor& color, int32 segments, float squashY,
        float thickness);

    // One text line of the HUD, each with its own cached FText
    enum class ESOLHudLine : uint8
    {
        DateWarp,
        Anchor,
        Nearest,
        SurfaceLock,
        Hints,
        MapPickHints,
        Speed,
        Frame,
        AbsoluteSpeed,
        Cap,
        Assist,
        ControlsNotice,
        GMeter,
        GWarning,
        Autopilot,
        AutopilotHint,
        AutopilotCancel,
        TargetName,
        TargetDistance,
        TargetRelative,
        TargetLabel,
        RadarSelected,
        RadarRange,
        Count
    };

    // The last text drawn on one line and the FText built from it (rebuilt only when the text changes)
    struct FSOLHudLineCache
    {
        FString Source;
        FText Text;
    };

    // Draws the text buffer as one HUD line at a position with a font and color, scaled by UI scale times textScale
    void DrawBuffer(ESOLHudLine line, const FLinearColor& color, float x, float y, UFont* font,
        float textScale = 1.0f);

    // Rebuilds the cached key hint lines from the applied key bindings (at begin play and after a rebind)
    void RebuildKeyHints();

    // Draws the center reticle
    void DrawReticle(const FVector2D& center);

    // Draws the virtual-joystick circle, dead zone and the current stick offset
    void DrawJoystick(const FVector2D& center, const ASOLShipPawn& pawn);

    // Draws the sim date and warp, anchor, nearest body and altitude, and the key hints (top left)
    void DrawInfoBlock();

    // Draws the relative and absolute speed, the cap and its fill bar, assist and boost (bottom left)
    void DrawSpeedBlock(const FSOLShipState& state, const FSOLShipControl& control);

    // Draws the pilot G meter (above the speed block), the G warning (under the reticle) and the autopilot marker
    // with its ETA and the high-warp hint (top centre); SDD 18 placeholders, #19 owns the final layout
    void DrawPilotBlock(const FSOLShipControl& control);

    // Draws the mouse-steer icon centred on a point: Free (an open cursor arrow), Held (a reticle), Locked (a reticle
    // with a lock tick); bright while the mouse steers, dim otherwise
    void DrawSteerIcon(const FVector2D& center, ESOLSteerIcon icon, bool bSteering);

    // Draws the selected target's bracket and readout, or an edge arrow when it is off-screen
    void DrawTarget(const FSOLHudView& view, const FSOLShipState& state);

    // Draws the prograde and retrograde markers of the velocity relative to the active frame
    void DrawVelocityMarkers(const FSOLHudView& view, const FSOLShipState& state);

    // Builds the radar contacts from the targeting candidates, picks the range and draws the scope (bottom right)
    void DrawRadar(const FSOLShipState& state);

    // Returns true when a target is selected (sets the radar's range floor)
    bool HasSelectedTarget() const;

    // Appends a speed with its auto-picked unit to the text buffer
    void AppendSpeed(double speedMps);

    // Draws the jump warp's radial streaks for a streak intensity and sequence time; returns the number drawn
    int32 DrawWarpStreaks(double intensity, double elapsedS);

    // One warp streak: its direction from the screen center, and where (0..1) and how fast it travels outward
    struct FSOLWarpStreak
    {
        FVector2D Direction = FVector2D::UnitX();
        double PhaseOffset = 0.0;
        double CyclesPerSecond = 1.0;
    };

    UPROPERTY(Transient)
    TObjectPtr<USOLSimClockSubsystem> SimClock;

    UPROPERTY(Transient)
    TObjectPtr<USOLBodyRegistrySubsystem> BodyRegistry;

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> AnchorSubsystem;

    UPROPERTY(Transient)
    TObjectPtr<USOLShipSubsystem> Ships;

    UPROPERTY(Transient)
    TObjectPtr<USOLTargetingSubsystem> Targeting;

    UPROPERTY(Transient)
    TObjectPtr<USOLMapModeSubsystem> MapMode;

    UPROPERTY(Transient)
    TObjectPtr<USOLJumpSubsystem> Jump;

    UPROPERTY(Transient)
    TObjectPtr<USOLSettingsSubsystem> Settings;

    FSOLMapBodyOverlay mMapBodyOverlay;             // Jump map: per-body icons and name labels
    FSOLMapPickOverlay mMapPickOverlay;             // Jump map: destination-pick disc, guide line and marker
    FString mText;                                  // Reused text buffer (Reset keeps its capacity)
    FSOLHudLineCache mLines[static_cast<int32>(ESOLHudLine::Count)];   // Per-line cached text
    TArray<FSOLRadarContact> mRadarContacts;        // Reused radar contacts, rebuilt in place each frame
    TArray<FSOLWarpStreak> mWarpStreaks;            // Jump warp streak table, built once
    double mLastRadarRangeM = 0.0;                  // Radar range of the last frame
    double mManualRadarRangeM = 0.0;                // Radar range while in MANUAL mode, reclamped every frame
    FString mFlightHintsOrbitsOn;                   // Key hint line with the orbit lines on (rebuilt on rebinds)
    FString mFlightHintsOrbitsOff;                  // ...and off
    FString mMapHints;                              // The jump map's first hint line
    SOLHudFormat::FHudKeyTexts mKeyTexts;           // Key-naming HUD texts, incl. the controls notice (rebinds)
    FDelegateHandle mKeyBindingsHandle;             // Binding to the settings' OnKeyBindingsChanged
    float mUiScale = 1.0f;                          // Layout and font scale for the current canvas height
    int32 mLastWarpStreaksDrawn = 0;                // Warp streaks drawn last frame
    ESOLSurfaceLockHudLine mLastSurfaceLockLine = ESOLSurfaceLockHudLine::None;  // Surface-lock line drawn last frame
    double mControlsNoticeEndS = -1.0;              // Real time until which the controls-changed notice shows
    bool mWasFlightHudDrawn = false;                // The last frame drew the flight HUD (not hidden by a warp)
    bool mIsRadarManual = false;                    // Radar range mode: false = AUTO (starts here), true = MANUAL
};
