/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Flight/SOLFlight.h"
#include "Flight/SOLFlightMode.h"
#include "HudEvents/SOLHudBus.h"
#include "Map/SOLMapBodyOverlay.h"
#include "Map/SOLMapHologramOverlay.h"
#include "Map/SOLMapPickOverlay.h"
#include "Map/SOLMapRouteOverlay.h"
#include "UI/SOLHudFormat.h"
#include "UI/SOLHudShapeBatch.h"
#include "UI/SOLHudTextList.h"
#include "UI/SOLRadarLayout.h"

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"

#include "SOLFlightHud.generated.h"

class ASOLOrbitLineVisuals;
class SSOLHudShapeLayer;
class UFont;
class UMaterialInstanceDynamic;
class USOLAnchorSubsystem;
class USOLHudEventSubsystem;
class USOLBodyRegistrySubsystem;
class USOLJumpSubsystem;
class USOLSettingsSubsystem;
class USOLMapModeSubsystem;
class USOLRouteSubsystem;
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
 * Hologram HUD (SDD 21 phases A and B): every flight-HUD element except the radar (phase C) and the jump map's
 * overlays goes into SSOLHudShapeLayer: lines, rings, bars and hologram panels into its glow-shape batch (one Slate
 * custom-vertex draw with M_SOLHudShape) and every text line into its glow-text list (engine fonts drawn by Slate in
 * the LT palette, over a dark panel backing per text block); sol.HudRenderer 0 draws all of it with Canvas instead.
 * The pawn is never included: its pilot snapshot and the HUD commands arrive through USOLHudEventSubsystem (SDD 21 D4).
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

    // Returns the number of radar contacts drawn above / below the ship-horizontal plane last frame (SDD 21 C)
    int32 GetLastRadarAboveCount() const { return mLastRadarAboveCount; }
    int32 GetLastRadarBelowCount() const { return mLastRadarBelowCount; }

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

    // Returns the route overlay (legs, markers and their screen positions; verification)
    const FSOLMapRouteOverlay& GetRouteOverlay() const { return mRouteOverlay; }

    // Returns the map hologram grid and stems overlay (verification)
    const FSOLMapHologramOverlay& GetHologramOverlay() const { return mHologramOverlay; }

    // Returns the route marker line drawn in the last frame, empty when none (verification)
    const FString& GetLastRouteLineText() const { return mLastRouteLineText; }

    // Returns the route marker line's screen rectangle of the last frame (verification)
    const FBox2D& GetLastRouteLineRect() const { return mLastRouteLineRect; }

    // Returns the key-hint lines' screen rectangle of the last frame (verification)
    const FBox2D& GetLastHintsRect() const { return mLastHintsRect; }

    // Returns the flight key hint line as drawn with the orbit lines off (verification)
    const FString& GetFlightKeyHints() const { return mFlightHintsOrbitsOff; }

    // Returns the HUD texts that name a key, as drawn (verification)
    const SOLHudFormat::FHudKeyTexts& GetKeyTexts() const { return mKeyTexts; }

    // Returns the layout and font scale of the last frame (canvas height times the settings' HUD scale; verification)
    float GetUiScale() const { return mUiScale; }

    // Returns true if the last frame drew the flight HUD (false while a jump warp hides it; verification)
    bool WasFlightHudDrawn() const { return mWasFlightHudDrawn; }

    // Returns true if the last frame drew with the shape renderer (SDD 21; verification)
    bool WasShapeRendererActive() const { return mWasShapeRendererActive; }

    // Returns the number of glow shapes the last frame queued (verification)
    int32 GetLastShapeCount() const;

    // Returns the number of glow-text lines the last frame queued (verification)
    int32 GetLastTextCount() const;

    // Returns the number of hologram panels the last frame queued (verification)
    int32 GetLastPanelCount() const { return mLastPanelCount; }

    // Returns whether the last DrawHUD drew the "Hold Space to steer" hint (verification, issue #31)
    bool WasSteerHintDrawn() const { return mWasSteerHintDrawn; }

    // Returns the colour the joystick circles were last drawn in (verification, issue #31)
    const FLinearColor& GetLastJoystickColor() const { return mLastJoystickColor; }

    // Returns the number of glow-text lines the shape layer's last paint actually drew (verification)
    int32 GetLastPaintedTextCount() const;

    // Returns the number of panel backings the shape layer's last paint actually drew (verification)
    int32 GetLastPaintedBackingCount() const;

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

    // Draws a glow line (shape renderer) or a Canvas line in colour x intensity
    void GlowLine(const FVector2D& a, const FVector2D& b, const FLinearColor& color, float intensity, float thickness);

    // Draws a glow ring (shape renderer) or a Canvas circle of line segments in colour x intensity
    void GlowRing(const FVector2D& center, float radiusPx, const FLinearColor& color, float intensity, float thickness,
        int32 segments);

    // Draws a filled glow box (a capsule as tall as the box; shape renderer) or a Canvas rectangle
    void GlowBox(float x, float y, float width, float height, const FLinearColor& color, float intensity);

    // Draws an axis-aligned glow ellipse ring (shape renderer) or a Canvas ellipse of line segments; radius Y is
    // radiusXPx x squashY (squashY in [0, 1])
    void GlowEllipse(const FVector2D& center, float radiusXPx, float squashY, const FLinearColor& color,
        float intensity);

    // Draws a dashed glow line from a to b with dashes starting at a (shape renderer: one dashed-line shape whose dash
    // fraction is SOL::Hologram::HUD_DASH_FRACTION of dashPx + gapPx; Canvas: SOLRadar::DashSegment's dashes)
    void GlowDashedLine(const FVector2D& a, const FVector2D& b, double dashPx, double gapPx, const FLinearColor& color,
        float intensity);

    // Draws a filled glow dot (shape renderer) or a Canvas square of the same size
    void GlowDot(const FVector2D& center, float radiusPx, const FLinearColor& color, float intensity);

    // Draws a fill bar: a faint track, the filled part (clamped to [0, 1]) and an end tick
    void DrawBar(float x, float y, double fill01, const FLinearColor& color);

    // Queues a hologram panel (glow shape and dark backing) around a content rectangle (shape renderer only)
    void AddHoloPanel(const FBox2D& content, const FLinearColor& color);

    // Returns the engine font of a HUD font slot
    static UFont* GetHudFont(ESOLHudFont font);

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
        Route,
        TargetName,
        TargetDistance,
        TargetRelative,
        TargetLabel,
        RadarSelected,
        RadarRange,
        SteerHint,
        Count
    };

    // The last text drawn on one line, the FText built from it and its unscaled measured size (all rebuilt only when
    // the text or font changes)
    struct FSOLHudLineCache
    {
        FString Source;
        FText Text;
        FVector2D Size = FVector2D::ZeroVector;
        ESOLHudFont Font = ESOLHudFont::Small;
        bool bMeasured = false;
    };

    // Brings a line's cache up to date with the text buffer and font; returns the text's unscaled size
    FVector2D MeasureLine(ESOLHudLine line, ESOLHudFont font);

    // Draws the text buffer as one HUD line (glow text with the shape renderer, else Canvas) at a position with a font
    // and colour, scaled by the UI scale times textScale; returns its screen rectangle
    FBox2D DrawBuffer(ESOLHudLine line, const FLinearColor& color, float x, float y, ESOLHudFont font,
        float textScale = 1.0f);

    // Draws the text buffer as one HUD line centred on centerX; returns its screen rectangle
    FBox2D DrawBufferCentered(ESOLHudLine line, const FLinearColor& color, float centerX, float y, ESOLHudFont font);

    // Rebuilds the cached key hint lines from the applied key bindings (at begin play and after a rebind)
    void RebuildKeyHints();

    // Draws the center reticle (glow shapes while the shape renderer is active, else Canvas lines; SDD 21 proof element)
    void DrawReticle(const FVector2D& center);

    // Handles a command from the HUD event bus (orbit lines, radar zoom)
    void HandleHudCommand(ESOLHudCommand command);

    // Creates the shape layer widget and its material and adds it to the game viewport (SDD 21)
    void CreateShapeLayer();

    // Removes the shape layer widget from the game viewport
    void DestroyShapeLayer();

    // Starts this frame's shape batch and pushes changed material parameters (strength, composite, view size)
    void BeginShapeFrame();

    // Returns the shape batch while the shape renderer is active (sol.HudRenderer 1 and the layer exists), else nullptr
    FSOLHudShapeBatch* GetActiveShapeBatch();

    // Returns true when the bus holds a pilot snapshot published within SOL::Hologram::HUD_PILOT_MAX_FRAME_LAG frames
    bool HasFreshPilot() const;

    // Draws the virtual-joystick circle, dead zone and the current stick offset
    void DrawJoystick(const FVector2D& center, const FSOLHudPilotSnapshot& pilot);

    // Draws the sim date and warp, anchor, nearest body and altitude, and the key hints (top left)
    void DrawInfoBlock();

    // Draws the relative and absolute speed, the cap and its fill bar, assist and boost (bottom left)
    void DrawSpeedBlock(const FSOLShipState& state, const FSOLShipControl& control);

    // Draws the pilot G meter (above the speed block), the G warning (under the reticle) and the autopilot marker
    // with its ETA and the high-warp hint (top centre); SDD 18 placeholders, #19 owns the final layout
    void DrawPilotBlock(const FSOLShipControl& control);

    // Fills the route overlay's inputs from the route and map-mode subsystems and draws it (map, or flight with P on)
    void DrawRouteOverlay(const FSOLMapOverlayView& overlayView);

    // Fills the hologram overlay's inputs from the map camera and the routes and draws the grid and stems (map only)
    void DrawHologramOverlay(const FSOLMapOverlayView& overlayView);

    // Draws the route marker (state, progress, total ETA) below the autopilot marker, moved below the key hints if it
    // would overlap them (top centre)
    void DrawRouteMarker();

    // Draws the mouse-steer icon centred on a point: Free (an open cursor arrow), Held (a reticle), Locked (a reticle
    // with a lock tick); bright while the mouse steers, dim otherwise
    void DrawSteerIcon(const FVector2D& center, ESOLSteerIcon icon, bool bSteering);

    // Draws the selected target's bracket and readout, or an edge arrow when it is off-screen
    void DrawTarget(const FSOLHudView& view, const FSOLShipState& state);

    // Draws the prograde and retrograde markers of the velocity relative to the active frame
    void DrawVelocityMarkers(const FSOLHudView& view, const FSOLShipState& state);

    // Builds the radar contacts from the targeting candidates, picks the range and draws the hologram scope (bottom
    // right): the ship-horizontal plane, range rings with a sonar pulse, the view cone (needs the view, else omitted)
    // and each budgeted contact's stem and blip, above the plane LT Primary, below LT Secondary dashed and hollow
    void DrawRadar(const FSOLHudView* view, const FSOLShipState& state);

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
    TObjectPtr<USOLRouteSubsystem> Routes;

    UPROPERTY(Transient)
    TObjectPtr<USOLSettingsSubsystem> Settings;

    UPROPERTY(Transient)
    TObjectPtr<USOLHudEventSubsystem> HudEvents;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> ShapeMaterial;     // M_SOLHudShape instance drawn by the shape layer

    FSOLMapBodyOverlay mMapBodyOverlay;             // Jump map: per-body icons and name labels
    FSOLMapPickOverlay mMapPickOverlay;             // Jump map: destination-pick disc, guide line and marker
    FSOLMapRouteOverlay mRouteOverlay;              // Routes and the draft (map, and flight with P on; SDD 20)
    FSOLMapHologramOverlay mHologramOverlay;        // Map grid plane and stems (SDD 20 hologram look)
    FString mLastRouteLineText;                     // Route marker drawn last frame (verification)
    FBox2D mLastRouteLineRect = FBox2D(ForceInit);  // Its screen rectangle
    FBox2D mLastHintsRect = FBox2D(ForceInit);      // Key-hint lines' screen rectangle of the last frame
    float mAutopilotBlockBottomY = 0.0f;            // Bottom of the autopilot marker block this frame, 0 if none
    FString mText;                                  // Reused text buffer (Reset keeps its capacity)
    FSOLHudLineCache mLines[static_cast<int32>(ESOLHudLine::Count)];   // Per-line cached text
    TArray<FSOLRadarContact> mRadarContacts;        // Reused radar contacts, rebuilt in place each frame
    TArray<int32> mRadarOrder;                      // Budgeted contacts to draw (indices), reused each frame
    int32 mLastRadarAboveCount = 0;                 // Contacts drawn above / below the plane last frame
    int32 mLastRadarBelowCount = 0;
    TArray<FSOLWarpStreak> mWarpStreaks;            // Jump warp streak table, built once
    double mLastRadarRangeM = 0.0;                  // Radar range of the last frame
    double mManualRadarRangeM = 0.0;                // Radar range while in MANUAL mode, reclamped every frame
    FString mFlightHintsOrbitsOn;                   // Key hint line with the orbit lines on (rebuilt on rebinds)
    FString mFlightHintsOrbitsOff;                  // ...and off
    FString mMapHints;                              // The jump map's first hint line
    SOLHudFormat::FHudKeyTexts mKeyTexts;           // Key-naming HUD texts, incl. the controls notice (rebinds)
    FDelegateHandle mKeyBindingsHandle;             // Binding to the settings' OnKeyBindingsChanged
    FDelegateHandle mHudCommandHandle;              // Binding to the HUD event bus's commands
    TSharedPtr<SSOLHudShapeLayer> mShapeLayer;      // Batched glow-shape widget over the game viewport (SDD 21)
    FSOLHudShapeBatch* mBatch = nullptr;            // This frame's shape batch while the shape renderer is active
    FSOLHudTextList* mTextList = nullptr;           // This frame's glow-text lines while the shape renderer is active
    FBox2D mLeftBlockRect = FBox2D(ForceInit);      // Speed and G block text this frame (its panel)
    FBox2D mTopBlockRect = FBox2D(ForceInit);       // Autopilot and route text this frame (its panel)
    int32 mLastPanelCount = 0;                      // Hologram panels queued this frame
    bool mWasSteerHintDrawn = false;                // The steer hint was drawn this frame
    FLinearColor mLastJoystickColor = FLinearColor::Transparent;   // Joystick circles' colour when last drawn
    bool mWasBackingDropped = false;                // A panel backing was dropped this frame (layer full)
    bool mIsOverflowWarned = false;                 // The capacity-overflow warning was logged (once per HUD)
    float mShapeStrength = -1.0f;                   // Material parameters last pushed (-1 = never)
    float mShapeComposite = -1.0f;
    FVector2f mShapeViewSize = FVector2f::ZeroVector;
    bool mWasShapeRendererActive = false;           // The last frame drew with the shape renderer
    float mUiScale = 1.0f;                          // Layout and font scale for the current canvas height
    int32 mLastWarpStreaksDrawn = 0;                // Warp streaks drawn last frame
    ESOLSurfaceLockHudLine mLastSurfaceLockLine = ESOLSurfaceLockHudLine::None;  // Surface-lock line drawn last frame
    double mControlsNoticeEndS = -1.0;              // Real time until which the controls-changed notice shows
    bool mWasFlightHudDrawn = false;                // The last frame drew the flight HUD (not hidden by a warp)
    bool mIsRadarManual = false;                    // Radar range mode: false = AUTO (starts here), true = MANUAL
};
