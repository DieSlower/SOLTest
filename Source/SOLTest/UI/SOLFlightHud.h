/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Flight/SOLFlight.h"
#include "Flight/SOLFlightMode.h"
#include "HudEvents/SOLHudBus.h"
#include "Interaction/SOLInteractionSubsystem.h"
#include "Map/SOLMapBodyOverlay.h"
#include "Map/SOLMapHologramOverlay.h"
#include "Map/SOLMapPickOverlay.h"
#include "Map/SOLMapRouteOverlay.h"
#include "UI/SOLHoloFrameBuild.h"
#include "UI/SOLHoloRadial.h"
#include "UI/SOLHudFormat.h"
#include "UI/SOLHudLines.h"
#include "UI/SOLHudShapeBatch.h"
#include "UI/SOLHudSymbols.h"
#include "UI/SOLHudTextList.h"
#include "UI/SOLOverlayIconLayer.h"
#include "UI/SOLRadarLayout.h"

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"

#include "SOLFlightHud.generated.h"

class ASOLOrbitLineVisuals;
class SSOLHudShapeLayer;
class UFont;
class USOLHoloMenuSubsystem;
class UMaterialInstanceDynamic;
class USOLAnchorSubsystem;
class USOLHudEventSubsystem;
class USOLInteractionSubsystem;
class USOLBodyRegistrySubsystem;
class USOLCombatSubsystem;
class USOLNpcSubsystem;
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

    // Shows the overlay profile tag ("OVERLAYS: PROFILE N") for a 0-based profile: stores the real time and formats the
    // text once (SDD 34 section 8); it shows for SOL::Overlay::PROFILE_TAG_SHOW_S, then fades
    void NotifyOverlayProfileShown(int32 profile);

    // Returns the overlay profile tag's text as last formatted (verification)
    const FString& GetProfileTagText() const { return mProfileTagText; }

    // Returns the overlay profile tag's alpha in the last frame, 0 when it was not drawn (verification)
    float GetLastProfileTagAlpha() const { return mLastProfileTagAlpha; }

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

    // Returns the flight view's overlay icon layer (SDD 22; verification)
    const FSOLOverlayIconLayer& GetIconLayer() const { return mIconLayer; }

    // Returns the radial menu sectors and the info card lines drawn last frame (SDD 22; verification)
    int32 GetLastRadialSectorCount() const { return mLastRadialSectorCount; }
    int32 GetLastInfoCardLineCount() const { return mLastInfoCardLineCount; }

    // Returns the hologram radial menu's anim and the state it last drew (kept while it closes; SDD 42; verification)
    const FSOLHoloRadialState& GetRadialHolo() const { return mRadialHolo; }

    // Returns the look the radial was drawn at last frame (SDD 42; verification)
    const FSOLHoloMenuLook& GetLastRadialLook() const { return mLastRadialLook; }

    // Returns the hologram menu strength the radial was drawn at last frame (-1: none drawn; verification)
    float GetLastRadialStrength() const { return mLastRadialStrength; }

    // Returns the glow pad of the radial's arcs last frame (-1: none drawn; verification)
    float GetLastRadialPadPx() const { return mLastRadialPadPx; }

    // Returns the highlighted sector's rim intensity last frame (-1: none drawn; verification)
    float GetLastRadialHighlightIntensity() const { return mLastRadialHighlightIntensity; }

    // Returns the NPC ship contacts the radar drew last frame (Ships on Radar; verification)
    int32 GetLastRadarShipCount() const { return mLastRadarShipCount; }

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

    // Returns true when the menu layer (the second shape layer, SDD 42 section 4.3) exists (verification)
    bool HasMenuLayer() const { return mMenuLayer.IsValid(); }

    // Returns the material the menu layer draws with, or null (verification: the hologram menu subsystem's MID)
    UObject* GetMenuLayerMaterial() const;

    // Returns the shapes queued on the menu layer this frame (verification)
    int32 GetLastMenuShapeCount() const;

    // Returns the backings queued on the menu layer this frame (verification)
    int32 GetLastMenuBackingCount() const;

    // Returns the shapes the menu layer's last paint drew (verification)
    int32 GetLastMenuPaintedShapeCount() const;

    // Returns the frame the menu layer was last stamped for (0: never; verification)
    uint64 GetMenuLayerBatchFrame() const;

    // Returns the text lines the menu layer's last paint drew (verification)
    int32 GetLastMenuPaintedTextCount() const;

    // Returns the backings the menu layer's last paint drew (verification)
    int32 GetLastMenuPaintedBackingCount() const;

    // What one card (ESOLHudCard, UI/SOLHudLines.h) drew last frame (verification)
    struct FSOLHudCardStats
    {
        bool bDrawn = false;                    // Built and drawn this frame
        bool bOnMenuLayer = false;              // On the menu layer (else the HUD layer: no menu material)
        int32 Shapes = 0;                       // Shapes the batch took
        int32 Ticks = 0;                        // Corner tick segments
        int32 Others = 0;                       // Shapes other than the fill, ticks and sweep (0: no outline)
        int32 Sweeps = 0;                       // Idle sweep lines
        float TickPadPx = -1.0f;                // Glow pad of the corner ticks (-1: none drawn)
        float BackingAlpha = -1.0f;             // Absolute alpha of its dark tint (-1: no backing)
        FBox2f Rect = FBox2f(ForceInit);        // The card rect (its backing)
        FSOLHoloMenuLook Look;                  // The look it was drawn at
        float Strength = 0.0f;                  // The hologram menu strength it was drawn at
        int32 ContentShapes = 0;                // Content shapes (bars, icon, G meter) added on the menu layer
        int32 ContentTexts = 0;                 // Content text lines added on the menu layer
        float ContentPadPx = -1.0f;             // Glow pad of its content lines (-1: no content on the menu layer)
    };

    // Returns what a card drew last frame (verification)
    const FSOLHudCardStats& GetLastCard(ESOLHudCard card) const;

    // Returns the info card's anim (the always-on cards have none; verification)
    const FSOLHoloMenuAnim& GetInfoCardAnim() const { return mInfoCardAnim; }

    // Returns the target card's anim (verification)
    const FSOLHoloMenuAnim& GetTargetCardAnim() const { return mTargetCardAnim; }

    // Returns the target name the target card shows (kept while it closes; verification)
    FName GetTargetCardName() const { return mTargetCardName; }

    // Returns how many of the HUD layer's backings this frame overlap a rectangle (verification: a card's rect)
    int32 CountHudBackingsOverlapping(const FBox2f& rect) const;

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

    // Draws a fill bar: a faint track, the filled part (clamped to [0, 1]) in a palette role and an end tick
    void DrawBar(float x, float y, double fill01, ESOLHudPaletteRole fillRole);

    // Replays symbol primitives in the HUD palette through the Glow* helpers and MenuArc in order (ShapeOnly ones only
    // with the shape renderer; a primitive's PadPx > 0 replaces the Glow* pad); returns the primitives drawn (an Arc
    // counts only when MenuArc took it)
    int32 ReplaySymbolPrims(const FSOLHudSymbolPrims& prims);

    // Queues a hologram panel (glow shape and dark backing) around a content rectangle (shape renderer only); the
    // backing's darkness is scaled by backingOpacity (0-1), for a panel that fades out
    void AddHoloPanel(const FBox2D& content, const FLinearColor& color, float backingOpacity = 1.0f);

    // Returns the engine font of a HUD font slot
    static UFont* GetHudFont(ESOLHudFont font);

    // Draws a circle (or an ellipse when squashY < 1) as line segments
    void DrawCircle(const FVector2D& center, float radiusPx, const FLinearColor& color, int32 segments, float squashY,
        float thickness);

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

    // Draws the overlay profile tag centred under the controls-notice line while its fade lasts (behind the HUD gates)
    void DrawOverlayProfileTag(double realTimeS);

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

    // Starts this frame's menu layer (SDD 42 section 4.3): empties it if the last frame drew on it, binds the menu
    // material once it exists and feeds the subsystem's material update; the layer is stamped only when written to
    void BeginMenuFrame();

    // Returns the batch menu shapes go to this frame, stamping the menu layer on first use: the menu layer's with the
    // menu material, else the HUD's own batch (no menu material), else nullptr (sol.HudRenderer 0: Canvas)
    FSOLHudShapeBatch* GetMenuBatch();

    // Draws the text buffer as one menu line on the menu layer, so it paints above the menu's backing and fill (else as
    // DrawBuffer: the HUD layer's text or Canvas); returns its screen rectangle
    FBox2D MenuText(ESOLHudLine line, const FLinearColor& color, float x, float y, ESOLHudFont font,
        float textScale = 1.0f);

    // Draws the text buffer as one menu line centred on centerX (as MenuText); returns its screen rectangle
    FBox2D MenuTextCentered(ESOLHudLine line, const FLinearColor& color, float centerX, float y, ESOLHudFont font);

    // Draws a menu glow arc on the menu layer (else the HUD batch); false when dropped or there is no batch (Canvas)
    bool MenuArc(const FVector2f& center, float radiusPx, float halfWidthPx, float midAngleRad, float halfApertureRad,
        const FLinearColor& color, float intensity, float padPx);

    // Draws a built hologram frame or card (shapes and backing) on the menu layer (else the HUD layer); returns the
    // shapes taken (0 under sol.HudRenderer 0: the Canvas fallback draws the menus as before, with no effects)
    int32 MenuHoloShapes(const FSOLHoloFrameOutput& output);

    // Sends the HUD's draw helpers (DrawBuffer, GlowLine, GlowBox, GlowRing, DrawBar) to the menu layer until
    // EndCardContent, so an always-on card's content paints above its own backing, its lines padded by the hologram
    // menu strength (LinePad: flat at 0, D18); false (nothing changed) when the menu layer is not in use this frame.
    // No AddHoloPanel, MenuHoloShapes or DrawCard call may run in between; BeginShapeFrame clears a missed End
    bool BeginCardContent(ESOLHudCard card);

    // Sends the HUD's draw helpers back to the HUD layer and records the content added into the card's stats
    void EndCardContent();

    // Builds a frameless card (SOLHoloFrameBuild::BuildCard: corner ticks, faint tint, sweep) around a content
    // rectangle at a look and draws it on the menu layer (else the HUD layer), recording what it drew
    void DrawCard(ESOLHudCard card, const FBox2D& content, const FSOLHoloMenuLook& look);

    // Returns a card's look this frame from its anim (whole and plain under the Canvas fallback while live)
    FSOLHoloMenuLook CardLook(const FSOLHoloMenuAnim& anim, bool bLive) const;

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

    // Draws the overlay icons and edge arrows (SDD 22) and chooses the NPC ships for the icons and the radar
    void DrawOverlayIcons(const FSOLHudView* view);

    // Draws the interaction menus' HUD parts (SDD 22 section 2.4): the info card and the action hint (flight and map)
    // and, in flight, the open radial menu
    void DrawInteraction();

    // Runs the hologram radial menu's anim for this frame from the interaction's state (SDD 42 section 5), every
    // DrawHUD before its early returns: caches the open menu, starts the open or the close (bShown false while the
    // flight HUD is not drawn or the map is open), follows the highlighted sector, plays the open, hover and select
    // cues and keeps the look DrawInteraction draws (always whole, with no effects, under the Canvas fallback)
    void UpdateRadialMenu(bool bShown);

    // Draws the radial menu from its cached state on the menu layer at a look: one sector per action around its
    // centre (rim arcs grown by the draw-in, the highlighted one glowing on hover), then the labels, the title and
    // the highlighted disabled action's reason faded by the fill
    void DrawRadialMenu(const FSOLHoloMenuLook& look, float s);

    // Draws the info card of the object Info was chosen for (top right) as a frameless card on the menu layer: it
    // draws in with the open cue when Info is chosen for a new object (the same object again changes nothing) and
    // closes from its cached object and facts (value types) once the card timed out (SDD 42 sections 4.5 and 5)
    void DrawInfoCard();

    // Draws the target card (the selected target's readout, top right) on the menu layer at its look, from the cached
    // name, distance and relative speed (kept while it closes)
    void DrawTargetCard(const FSOLHoloMenuLook& look);

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
    TObjectPtr<USOLCombatSubsystem> Combat;

    UPROPERTY(Transient)
    TObjectPtr<USOLNpcSubsystem> Npcs;

    UPROPERTY(Transient)
    TObjectPtr<USOLInteractionSubsystem> Interaction;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> ShapeMaterial;     // M_SOLHudShape instance drawn by the shape layer

    UPROPERTY(Transient)
    TObjectPtr<USOLHoloMenuSubsystem> HoloMenu;             // Owns the menu material the menu layer draws with

    FSOLMapBodyOverlay mMapBodyOverlay;             // Jump map: per-body icons and name labels
    FSOLOverlayIconLayer mIconLayer;                // Flight view overlay icons (SDD 22)
    bool mAreShipsSelected = false;                 // The icon layer chose NPC ships this frame (icons or radar)
    int32 mLastRadarShipCount = 0;                  // NPC ship contacts on the radar last frame
    int32 mLastRadialSectorCount = 0;               // Radial menu sectors drawn last frame (SDD 22)
    int32 mLastInfoCardLineCount = 0;               // Info card lines drawn last frame
    FSOLHoloRadialState mRadialHolo;                // Hologram radial: anim and the last drawn state (SDD 42)
    FSOLHoloMenuLook mLastRadialLook;               // Its look, strength, arc pad and highlighted rim glow last frame
    float mLastRadialStrength = 0.0f;
    float mLastRadialPadPx = 0.0f;
    float mLastRadialHighlightIntensity = -1.0f;
    uint64 mLastRadialUpdateFrame = 0;              // Frame of the radial's last update (a gap closes it at once)
    FName mRadialTitleName;                         // The open radial's title (its object's name, else its type),
    ESOLObjectType mRadialTitleType = ESOLObjectType::Count;   // kept to draw the close after the object went away
    int32 mRadialTitleOpenCount = INDEX_NONE;       // The radial open count the title was looked up for
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
    TSharedPtr<SSOLHudShapeLayer> mMenuLayer;       // Second shape layer, menu material, just above the HUD's (SDD 42)
    bool mIsMenuFrameStamped = false;               // The menu layer was written to (and stamped) this frame
    FSOLHudShapeBatch* mCardHudBatch = nullptr;     // The HUD layer's batch and text list while BeginCardContent sends
    FSOLHudTextList* mCardHudTextList = nullptr;    // the draw helpers to the menu layer (null otherwise)
    ESOLHudCard mContentCard = ESOLHudCard::Count;  // The card whose content is being drawn, with the menu layer's
    int32 mContentShapeStart = 0;                   // shape and text counts when it began
    int32 mContentTextStart = 0;
    float mGlowPadPx = SOL::Hologram::HUD_GLOW_PAD_PX;   // Glow pad of the Glow* helpers (LinePad on card content)
    FSOLHudSymbolPrims mSymbolScratch;              // One HUD element's symbol primitives, reset before each build
                                                    // (about 2.8 KB: a member, never a local)
    FSOLHoloFrameOutput mCardOutput;                // One card's build, reused (8 KB: a member, never a local)
    FSOLHudCardStats mCardStats[static_cast<int32>(ESOLHudCard::Count)];   // What each card drew this frame
    double mCardNowS = 0.0;                         // Real time and hologram menu strength the cards use this frame
    float mCardStrength = 0.0f;
    FSOLHoloMenuLook mAlwaysOnCardLook;             // The speed and date-and-anchor cards' look this frame (Open)
    FSOLHoloMenuAnim mInfoCardAnim;                 // Info card: anim, the object and facts last drawn (kept while it
    FSOLObjectRef mInfoCardObject;                  // closes) and the frame of its last update (a gap closes it at
    FSOLObjectInfo mInfoCardInfo;                   // once)
    uint64 mInfoCardFrame = 0;
    FSOLHoloMenuAnim mTargetCardAnim;               // Target card: anim, the target and readout last drawn (kept
    FName mTargetCardName;                          // while it closes) and the frame of its last update
    double mTargetCardDistanceM = 0.0;
    double mTargetCardRelativeMps = 0.0;
    uint64 mTargetCardFrame = 0;
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
    double mProfileTagStartS = -1.0;                // Real time of the last profile cycle press (-1 = never)
    FString mProfileTagText;                        // The profile tag as formatted at that press (reserved buffer)
    float mLastProfileTagAlpha = 0.0f;              // The tag's alpha last frame, 0 when not drawn
    bool mWasFlightHudDrawn = false;                // The last frame drew the flight HUD (not hidden by a warp)
    bool mIsRadarManual = false;                    // Radar range mode: false = AUTO (starts here), true = MANUAL
};
