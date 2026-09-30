/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Flight/SOLFlight.h"
#include "Map/SOLMapBodyOverlay.h"
#include "Map/SOLMapPickOverlay.h"
#include "UI/SOLRadarLayout.h"

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"

#include "SOLFlightHud.generated.h"

class ASOLShipPawn;
class UFont;
class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLMapModeSubsystem;
class USOLShipSubsystem;
class USOLSimClockSubsystem;
class USOLTargetingSubsystem;

/**
 * Sim-style flight HUD (SDD 2, sub-part 1c), drawn every frame on the HUD Canvas with no editor assets: reticle and
 * virtual-joystick indicator, speed relative to the active frame (absolute beneath), speed cap and fill bar, assist
 * and boost state, frame and anchor names, nearest body and altitude, sim UTC date and warp, the selected target's
 * bracket (or an edge arrow when off-screen), prograde/retrograde markers, a 3D spherical radar (AUTO range = the
 * target-dependent floor, or MANUAL decade zoom with '-' / '=' and Home back to AUTO) and, toggled by O, every body's
 * orbit ellipse. While the jump map is open the pilot aids are hidden, the hint line shows the map's keys and every
 * body too small on screen gets a colored icon and name label (FSOLMapBodyOverlay), with the destination pick's disc,
 * height guide line and marker drawn over them (FSOLMapPickOverlay); the orbit ellipses and target
 * bracket project through whichever camera is active. Every number is read from the universe, ship and targeting
 * subsystems. The draw path reuses member buffers (text, radar contacts, orbit points) and keeps one cached FText per
 * text line, rebuilt only when that line's text changes; what still allocates is the FText of a changed line and
 * whatever Canvas does internally to draw text.
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

    // Toggles the body orbit ellipses (O); they are sampled again when turned on
    void ToggleOrbitLines();

    // Returns true while the body orbit ellipses are shown
    bool AreOrbitLinesVisible() const { return mShowOrbitLines; }

    // Returns the number of orbit-line segments drawn in the last frame (verification)
    int32 GetLastOrbitSegmentsDrawn() const { return mLastOrbitSegmentsDrawn; }

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

protected:

    // Caches the subsystems and sizes the per-body orbit point buffers once
    virtual void BeginPlay() override;

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
        Hints,
        MapPickHints,
        Speed,
        Frame,
        AbsoluteSpeed,
        Cap,
        Assist,
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

    // Draws the center reticle
    void DrawReticle(const FVector2D& center);

    // Draws the virtual-joystick circle, dead zone and the current stick offset
    void DrawJoystick(const FVector2D& center, const ASOLShipPawn& pawn);

    // Draws the sim date and warp, anchor, nearest body and altitude, and the key hints (top left)
    void DrawInfoBlock();

    // Draws the relative and absolute speed, the cap and its fill bar, assist and boost (bottom left)
    void DrawSpeedBlock(const FSOLShipState& state, const FSOLShipControl& control);

    // Draws the selected target's bracket and readout, or an edge arrow when it is off-screen
    void DrawTarget(const FSOLHudView& view, const FSOLShipState& state);

    // Draws the prograde and retrograde markers of the velocity relative to the active frame
    void DrawVelocityMarkers(const FSOLHudView& view, const FSOLShipState& state);

    // Builds the radar contacts from the targeting candidates, picks the range and draws the scope (bottom right)
    void DrawRadar(const FSOLShipState& state);

    // Returns true when a target is selected (sets the radar's range floor)
    bool HasSelectedTarget() const;

    // Samples every body's orbit ellipse (parent-relative) into the reused point buffers
    void RefreshOrbitCache();

    // Draws every body's orbit ellipse, clipped at the near plane and to the screen
    void DrawOrbitLines(const FSOLHudView& view);

    // Draws one 3D segment given in camera-local cm, clipped at the near plane and to the canvas; true if drawn
    bool DrawLocalSegment(const FSOLHudView& view, FVector localA, FVector localB, const FLinearColor& color);

    // Appends a speed with its auto-picked unit to the text buffer
    void AppendSpeed(double speedMps);

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

    FSOLMapBodyOverlay mMapBodyOverlay;             // Jump map: per-body icons and name labels
    FSOLMapPickOverlay mMapPickOverlay;             // Jump map: destination-pick disc, guide line and marker
    FString mText;                                  // Reused text buffer (Reset keeps its capacity)
    FSOLHudLineCache mLines[static_cast<int32>(ESOLHudLine::Count)];   // Per-line cached text
    TArray<FSOLRadarContact> mRadarContacts;        // Reused radar contacts, rebuilt in place each frame
    TArray<TArray<FVector3d>> mOrbitPoints;         // Per-body parent-relative ellipse points (ecliptic, m)
    double mOrbitSampledAtS = 0.0;                  // Real time the orbit points were last sampled
    double mLastRadarRangeM = 0.0;                  // Radar range of the last frame
    double mManualRadarRangeM = 0.0;                // Radar range while in MANUAL mode, reclamped every frame
    float mUiScale = 1.0f;                          // Layout and font scale for the current canvas height
    int32 mLastOrbitSegmentsDrawn = 0;              // Orbit segments drawn last frame
    bool mShowOrbitLines = false;                   // O toggle; off by default (SDD 2)
    bool mHasOrbitSamples = false;                  // True once the orbit points were sampled for this toggle-on
    bool mIsRadarManual = false;                    // Radar range mode: false = AUTO (starts here), true = MANUAL
};
