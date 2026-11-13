/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLFlightHud.h"

#include "Combat/SOLCombatSubsystem.h"
#include "Flight/SOLNewtonianFlight.h"
#include "Game/SOLGameMode.h"
#include "HudEvents/SOLHudEventSubsystem.h"
#include "Interaction/SOLInteractionSubsystem.h"
#include "Level/SOLSurfaceLock.h"
#include "Map/SOLJumpSubsystem.h"
#include "Map/SOLMapModeSubsystem.h"
#include "Map/SOLMapPicking.h"
#include "Map/SOLRouteSubsystem.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "Npc/SOLNpcSubsystem.h"
#include "Npc/SOLNpcTierProcessor.h"
#include "OrbitLines/SOLOrbitLineVisuals.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "UI/SOLHoloFrameBuild.h"
#include "UI/SOLHoloMenuSubsystem.h"
#include "UI/SOLHudFormat.h"
#include "UI/SOLHudShapeLayer.h"
#include "UI/SOLHudShapeMath.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLRenderPlacement.h"
#include "Universe/SOLSimClockSubsystem.h"
#include "Visuals/SOLHologramVisuals.h"

#include "Camera/PlayerCameraManager.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RandomStream.h"

// Game-thread cost of the lane tunnel wall labels (stat dumpave in -SOLSmokeNpcTunnelProfile, SDD 49 section 4.8)
DECLARE_STATS_GROUP(TEXT("SOLHud"), STATGROUP_SOLHud, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("SOL_HudLaneLabels"), STAT_SOL_HudLaneLabels, STATGROUP_SOLHud);

namespace
{
    // Layout at a 720-pixel-high canvas (scaled up with the canvas height, see SOL::Hologram::HUD_REFERENCE_HEIGHT_PX)
    constexpr float HUD_MARGIN_PX = 16.0f;
    constexpr float HUD_LINE_LARGE_PX = 36.0f;
    constexpr float HUD_SPEED_TEXT_SCALE = 1.8f;             // The main speed readout, on top of the UI scale
    constexpr float HUD_LINE_MEDIUM_PX = 20.0f;
    constexpr float HUD_LINE_SMALL_PX = 16.0f;
    constexpr float HUD_TARGET_PANEL_WIDTH_PX = 250.0f;

    // The symbols' own geometry (reticle, joystick, bracket, markers, radar, bars, streaks) is SOL::HudGeometry, built
    // by SOLHudSymbols. The target edge arrow keeps this margin from the screen edge (and a target this far in)
    constexpr float HUD_EDGE_ARROW_MARGIN_PX = 40.0f;

    // Prograde/retrograde markers (shown above SOL::Hologram::HUD_VELOCITY_MARKER_MIN_MPS of relative speed): the
    // projection distance of the velocity direction
    constexpr double HUD_DIRECTION_PROJECT_CM = 1.0e6;   // Any distance works for a pure direction

    // Projection: points closer than this in front of the camera (cm) are clipped
    constexpr double HUD_NEAR_PLANE_CM = 10.0;

    // Hologram radar (SDD 21 phase C): the dimming of a contact behind the camera or beyond the range, the blip merge
    // distance and the view cone's cut-off
    constexpr float HUD_RADAR_BEHIND_INTENSITY = 0.7f;
    constexpr float HUD_RADAR_BEYOND_INTENSITY = 0.45f; // A contact beyond the range (pulled onto the rim) is dimmer
    constexpr float HUD_RADAR_MERGE_PX = 14.0f;         // A blip this close to a drawn one is skipped (stacked moons)
    constexpr double HUD_RADAR_CONE_MIN_LATERAL = 0.1;  // View cone hidden when the camera looks this near ship-up/down

    // The glow dashed line's dash fraction (SOL::Hologram::HUD_DASH_FRACTION 0.5) assumes equal dashes and gaps
    static_assert(SOLRadar::StemDashPx == SOLRadar::StemGapPx, "Dashed stems: the glow shape assumes dash == gap");

    // LT palette (SDD 21 phase B). A tone is a palette colour and an intensity: glow shapes scale their glow by it,
    // Canvas lines (which ignore alpha) their RGB, and text lines take a dim tone as alpha (HudDimText)
    const FLinearColor& HUD_PRIMARY = SOL::HudPalette::PRIMARY;
    const FLinearColor& HUD_SECONDARY = SOL::HudPalette::SECONDARY;   // Warnings, the target, the locked frame
    const FLinearColor& HUD_BAD = SOL::HudPalette::BAD;               // G critical / blackout / lethal (SDD 18)
    constexpr float HUD_TEXT_DIM_ALPHA = 0.7f;                          // Dim text lines (hints, absolute speed)
    constexpr float HUD_PANEL_GAP_PX = 8.0f;                            // Info panel to the key hints below it
    const FLinearColor HUD_BAR_BACK_COLOR(0.0f, 0.0f, 0.0f, 0.45f);
    constexpr float HUD_G_BLOCK_GAP_PX = 8.0f;                              // Gap between the G meter and speed block
    constexpr float HUD_G_WARNING_Y_FRACTION = 0.62f;                       // G warning: this far down the screen
    constexpr float HUD_AUTOPILOT_TOP_PX = 64.0f;                           // Autopilot marker below the top edge
    constexpr float HUD_ROUTE_HINT_GAP_PX = 2.0f;                           // Route marker gap below the key hints
    constexpr float HUD_STEER_ICON_GAP_PX = 10.0f;                          // Gap between the mode line and the icon
    constexpr double HUD_CONTROLS_NOTICE_S = 15.0;                          // One-time controls notice duration
    constexpr float HUD_CONTROLS_NOTICE_Y_FRACTION = 0.2f;                  // Controls notice: this far down
    constexpr float HUD_PROFILE_TAG_GAP_PX = 6.0f;      // Overlay profile tag (SDD 34): its panel this far below the
                                                        // controls notice's panel
    constexpr int32 HUD_PROFILE_TAG_RESERVE = 32;       // Characters reserved for the tag text
    constexpr double HUD_PERCENT = 100.0;
    const FLinearColor HUD_RADAR_BACK_COLOR(0.0f, 0.02f, 0.05f, 0.35f);   // Radar backing (Canvas fallback)
    constexpr float HUD_RADAR_BACKING_GROW = 0.25f;     // Radar disc backing: grown by this x the squashed radius and
                                                        // feathered over twice that (a soft stadium, issue #31)
    constexpr float HUD_STEER_HINT_Y_FRACTION = 0.8f;   // "Hold Space to steer": this far down the screen (issue #31)

    // Jump warp streaks (their geometry is SOL::HudGeometry::WARP_STREAK_*): cycle rates vary per streak; fixed seed
    // for a stable pattern
    constexpr double WARP_STREAK_MIN_CYCLES_PER_S = 0.9;
    constexpr double WARP_STREAK_MAX_CYCLES_PER_S = 1.8;
    constexpr int32 WARP_STREAK_SEED = 2026;

    // Shape renderer (SDD 21): its layer under the menus and the F3 panel
    constexpr int32 HUD_SHAPE_LAYER_Z_ORDER = 0;
    constexpr int32 HUD_MENU_LAYER_Z_ORDER = 1;                 // The menu layer: above the HUD layer, below the menus

    // sol.HudRenderer: 1 draws the ported HUD pieces with the glow-shape renderer, 0 falls back to Canvas
    TAutoConsoleVariable<int32> CVarHudRenderer(SOL::Hologram::CVAR_HUD_RENDERER, 1,
        TEXT("Flight HUD glow-shape renderer (SDD 21): 1 = on, 0 = the old Canvas drawing"), ECVF_Default);

    // sol.HudComposite: 1 adds LT's grain and scanlines to the glow shapes (off by default)
    TAutoConsoleVariable<int32> CVarHudComposite(SOL::Hologram::CVAR_HUD_COMPOSITE, 0,
        TEXT("Flight HUD grain and scanline composite (SDD 21): 1 = on, 0 = off"), ECVF_Default);

    // sol.HudTextGlow: 1 draws the HUD text in the glow style, 0 plain over its drop shadow (shape renderer only)
    TAutoConsoleVariable<int32> CVarHudTextGlow(SOL::Hologram::CVAR_HUD_TEXT_GLOW, 1,
        TEXT("Flight HUD glow text (SDD 21): 1 = glow style, 0 = plain text over a drop shadow"), ECVF_Default);

    //////////////////////////////////////////////////////////////////////////
    // Returns a colour's RGB scaled by an intensity (opaque), for Canvas lines and boxes
    FLinearColor HudScaled(const FLinearColor& color, const float intensity)
    {
        return FLinearColor(color.R * intensity, color.G * intensity, color.B * intensity, 1.0f);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the lane and tunnel pixel scale, the viewport height over the lanes' reference height (1 for none)
    double HudLaneHeightScale(const double viewportHeightPx)
    {
        return viewportHeightPx > 0.0 ? viewportHeightPx / SOL::NPC_LANE_REFERENCE_HEIGHT_PX : 1.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when a screen point lies on the viewport (a label anchor off it can place no box)
    bool HudIsOnViewport(const FVector2D& screenPx, const double widthPx, const double heightPx)
    {
        return screenPx.X >= 0.0 && screenPx.X <= widthPx && screenPx.Y >= 0.0 && screenPx.Y <= heightPx;
    }

    //////////////////////////////////////////////////////////////////////////
    // Clips a camera-local segment (cm, X forward) to the near plane and the viewport's side planes grown by a pad
    // (Liang-Barsky); false when nothing of it is left
    bool HudClipSegmentToView(FVector& a, FVector& b, const FVector2D& centerPx, const double focalPx,
        const double widthPx, const double heightPx, const double nearCm, const double padPx)
    {
        // Each plane as n . p + c >= 0 inside: near, left, right, top, bottom (screen = centre + f (Y, -Z) / X)
        const FVector4 planes[5] = {
            FVector4(1.0, 0.0, 0.0, -nearCm),
            FVector4(centerPx.X + padPx, focalPx, 0.0, 0.0),
            FVector4(widthPx - centerPx.X + padPx, -focalPx, 0.0, 0.0),
            FVector4(centerPx.Y + padPx, 0.0, -focalPx, 0.0),
            FVector4(heightPx - centerPx.Y + padPx, 0.0, focalPx, 0.0),
        };
        double t0 = 0.0;
        double t1 = 1.0;
        for (const FVector4& plane : planes)
        {
            const double fa = plane.X * a.X + plane.Y * a.Y + plane.Z * a.Z + plane.W;
            const double fb = plane.X * b.X + plane.Y * b.Y + plane.Z * b.Z + plane.W;
            if (fa < 0.0 && fb < 0.0)
            {
                return false;
            }
            if (fa < 0.0)
            {
                t0 = FMath::Max(t0, fa / (fa - fb));
            }
            else if (fb < 0.0)
            {
                t1 = FMath::Min(t1, fa / (fa - fb));
            }
        }
        if (t0 > t1)
        {
            return false;
        }
        const FVector delta = b - a;
        b = a + delta * t1;
        a = a + delta * t0;
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when the segment from the camera to a point (universe ecliptic m) passes inside a registry body
    // (the camera outside it): the CPU side of the tunnels' sphere occlusion, for the label anchors
    bool HudIsSegmentBlockedByBody(const FSOLBodyRegistry& registry, const FVector3d& cameraM,
        const FVector3d& pointM)
    {
        const FVector3d toPoint = pointM - cameraM;
        const double lengthSq = toPoint.SizeSquared();
        for (int32 body = 0; body < registry.Num(); ++body)
        {
            const double radiusM = registry.GetRadiusM(body);
            const FVector3d toCenter = registry.GetPositionM(body) - cameraM;
            const double radiusSq = radiusM * radiusM;
            if (!(radiusM > 0.0) || toCenter.SizeSquared() <= radiusSq)
            {
                continue;
            }
            const double t = lengthSq > 0.0
                ? FMath::Clamp(FVector3d::DotProduct(toCenter, toPoint) / lengthSq, 0.0, 1.0)
                : 0.0;
            if ((toCenter - toPoint * t).SizeSquared() < radiusSq)
            {
                return true;
            }
        }
        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a text colour dimmed through its alpha
    FLinearColor HudDimText(const FLinearColor& color)
    {
        return FLinearColor(color.R, color.G, color.B, HUD_TEXT_DIM_ALPHA);
    }

    //////////////////////////////////////////////////////////////////////////
    // Runs a HUD card's anim for one frame (SDD 42 section 4.5): a new object restarts it from Closed, a live card
    // opens (a closing one reverses), a gone one closes, and one gone while HUD frames were skipped closes at once
    // (never a stale close later); returns true when an open began (the info card's open cue)
    bool HudCardUpdate(FSOLHoloMenuAnim& anim, const bool bLive, const bool bNewObject, const bool bContinuous,
        const double nowS, const float s)
    {
        bool bOpened = false;
        if (bLive)
        {
            if (bNewObject)
            {
                anim = FSOLHoloMenuAnim();
            }
            bOpened = SOLHoloMenu::BeginOpen(anim, nowS, s);
        }
        else if (bContinuous)
        {
            SOLHoloMenu::BeginClose(anim, nowS, s);
        }
        else
        {
            anim = FSOLHoloMenuAnim();
        }
        SOLHoloMenu::Advance(anim, nowS, s);
        return bOpened;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the HUD's symbol palette (SOLHudSymbols::HudPalette, built once)
    const FSOLHudSymbolPalette& HudSymbolPalette()
    {
        static const FSOLHudSymbolPalette palette = SOLHudSymbols::HudPalette();
        return palette;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a palette role's HUD colour (Primary for a role past the palette)
    const FLinearColor& HudRoleColor(const ESOLHudPaletteRole role)
    {
        const int32 index = static_cast<int32>(role);
        return HudSymbolPalette().Colors[index < static_cast<int32>(ESOLHudPaletteRole::Count) ? index
            : static_cast<int32>(ESOLHudPaletteRole::Primary)];
    }
}

//////////////////////////////////////////////////////////////////////////
// Disables ticking (all work happens in DrawHUD)
ASOLFlightHud::ASOLFlightHud()
{
    PrimaryActorTick.bCanEverTick = false;
}

//////////////////////////////////////////////////////////////////////////
// Caches the subsystems and sizes the per-body orbit point buffers once
void ASOLFlightHud::BeginPlay()
{
    Super::BeginPlay();
    const UWorld* world = GetWorld();
    SimClock = world->GetSubsystem<USOLSimClockSubsystem>();
    BodyRegistry = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    AnchorSubsystem = world->GetSubsystem<USOLAnchorSubsystem>();
    Ships = world->GetSubsystem<USOLShipSubsystem>();
    Targeting = world->GetSubsystem<USOLTargetingSubsystem>();
    MapMode = world->GetSubsystem<USOLMapModeSubsystem>();
    Jump = world->GetSubsystem<USOLJumpSubsystem>();
    Routes = world->GetSubsystem<USOLRouteSubsystem>();
    Settings = USOLSettingsSubsystem::Get(this);
    HudEvents = world->GetSubsystem<USOLHudEventSubsystem>();
    Combat = world->GetSubsystem<USOLCombatSubsystem>();
    Npcs = world->GetSubsystem<USOLNpcSubsystem>();
    Interaction = world->GetSubsystem<USOLInteractionSubsystem>();
    HoloMenu = world->GetSubsystem<USOLHoloMenuSubsystem>();

    // The pawn talks to the HUD only through the HUD event bus (SDD 21 D4): commands in, the map overlay out
    if (HudEvents != nullptr)
    {
        mHudCommandHandle = HudEvents->GetBus().OnCommand().AddUObject(this, &ASOLFlightHud::HandleHudCommand);
        HudEvents->GetBus().SetMapBodyOverlay(&mMapBodyOverlay);
    }
    CreateShapeLayer();

    // Key hints follow the player's bindings: built now and again after each applied rebind, never per frame
    RebuildKeyHints();
    if (Settings != nullptr)
    {
        mKeyBindingsHandle = Settings->OnKeyBindingsChanged().AddUObject(this, &ASOLFlightHud::RebuildKeyHints);
    }

    // Buffers sized once so the draw path only resets and refills them
    mText.Reserve(256);
    mLastRouteLineText.Reserve(128);
    mProfileTagText.Reserve(HUD_PROFILE_TAG_RESERVE);

    // Warp streak table: random directions, phases and speeds from a fixed seed
    FRandomStream random(WARP_STREAK_SEED);
    mWarpStreaks.SetNum(SOL::WARP_STREAK_COUNT);
    for (FSOLWarpStreak& streak : mWarpStreaks)
    {
        const double angleRad = random.FRandRange(0.0, UE_DOUBLE_TWO_PI);
        streak.Direction = FVector2D(FMath::Cos(angleRad), FMath::Sin(angleRad));
        streak.PhaseOffset = random.FRand();
        streak.CyclesPerSecond = random.FRandRange(WARP_STREAK_MIN_CYCLES_PER_S, WARP_STREAK_MAX_CYCLES_PER_S);
    }
    if (BodyRegistry != nullptr)
    {
        const int32 bodyCount = BodyRegistry->GetRegistry().Num();
        const int32 radarCapacity = bodyCount + SOL::TARGETING_RESERVED_TARGETABLES + SOL::Overlay::MAX_SHIP_ICONS;
        mRadarContacts.Reserve(radarCapacity);
        mRadarOrder.Reserve(radarCapacity);
        mMapBodyOverlay.Initialize(BodyRegistry->GetRegistry());
        mIconLayer.Initialize(BodyRegistry->GetRegistry());
    }
    mMapPickOverlay.Initialize();
    mRouteOverlay.Initialize();
    mHologramOverlay.Initialize();
}

//////////////////////////////////////////////////////////////////////////
// Unhooks from the settings' key-binding changes
void ASOLFlightHud::EndPlay(const EEndPlayReason::Type endPlayReason)
{
    if (Settings != nullptr)
    {
        Settings->OnKeyBindingsChanged().Remove(mKeyBindingsHandle);
    }
    mKeyBindingsHandle.Reset();
    if (HudEvents != nullptr)
    {
        HudEvents->GetBus().OnCommand().Remove(mHudCommandHandle);
        if (HudEvents->GetBus().GetMapBodyOverlay() == &mMapBodyOverlay)
        {
            HudEvents->GetBus().SetMapBodyOverlay(nullptr);
        }
    }
    mHudCommandHandle.Reset();
    DestroyShapeLayer();
    Super::EndPlay(endPlayReason);
}

//////////////////////////////////////////////////////////////////////////
// Handles a command from the HUD event bus (orbit lines, radar zoom)
void ASOLFlightHud::HandleHudCommand(const ESOLHudCommand command)
{
    switch (command)
    {
    case ESOLHudCommand::ToggleOrbitLines:
        ToggleOrbitLines();
        break;
    case ESOLHudCommand::RadarZoomIn:
        StepRadarZoom(true);
        break;
    case ESOLHudCommand::RadarZoomOut:
        StepRadarZoom(false);
        break;
    case ESOLHudCommand::RadarAuto:
        ResetRadarAuto();
        break;
    case ESOLHudCommand::ShowOverlayProfileTag:
        NotifyOverlayProfileShown(Settings != nullptr ? Settings->GetOverlayPrefs().ActiveProfile : 0);
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Shows the overlay profile tag for a profile (0-based): remembers the real time and formats the text once
void ASOLFlightHud::NotifyOverlayProfileShown(const int32 profile)
{
    const UWorld* world = GetWorld();
    mProfileTagStartS = world != nullptr ? world->GetRealTimeSeconds() : 0.0;

    // "{N}" in the format is the 1-based profile number; the buffer was reserved at begin play
    const TCHAR* format = SOLMenuText::HUD_OVERLAY_PROFILE_TAG_FORMAT;
    const TCHAR* token = FCString::Strstr(format, SOLMenuText::OVERLAY_PROFILE_NUMBER_TOKEN);
    mProfileTagText.Reset();
    if (token == nullptr)
    {
        mProfileTagText.Append(format);
    }
    else
    {
        mProfileTagText.Append(format, static_cast<int32>(token - format));
        mProfileTagText.AppendInt(profile + 1);
        mProfileTagText.Append(token + FCString::Strlen(SOLMenuText::OVERLAY_PROFILE_NUMBER_TOKEN));
    }
    UE_LOG(LogSOL, Log, TEXT("FlightHud %s: overlay profile tag '%s'"), *GetName(), *mProfileTagText);
}

//////////////////////////////////////////////////////////////////////////
// Creates the shape layer widget, its material and its fonts and adds it to the game viewport
void ASOLFlightHud::CreateShapeLayer()
{
    UGameViewportClient* viewport = GetWorld()->GetGameViewport();
    UMaterialInterface* material = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::HUD_SHAPE_MATERIAL);
    if (viewport == nullptr || material == nullptr || GEngine == nullptr)
    {
        UE_LOG(LogSOL, Warning, TEXT("FlightHud %s: no %s; the HUD draws with Canvas only"), *GetName(),
            viewport == nullptr ? TEXT("game viewport") : SOL::Paths::HUD_SHAPE_MATERIAL);
        return;
    }
    ShapeMaterial = UMaterialInstanceDynamic::Create(material, this);
    mShapeLayer = SNew(SSOLHudShapeLayer);
    mShapeLayer->SetMaterial(ShapeMaterial);

    // The menu layer (SDD 42 section 4.3): the same widget with the hologram menu material (bound in BeginMenuFrame,
    // the subsystem creates it at world begin play), just above the HUD layer and below the UMG menus; it holds the
    // menus' and cards' shapes, backings and text, so their text paints above their own backing
    mMenuLayer = SNew(SSOLHudShapeLayer).MaxShapes(SOL::HoloMenu::MENU_LAYER_MAX_SHAPES);

    // The text lines use the same engine fonts and sizes Canvas draws them with (GEngine's fonts stay loaded)
    for (int32 index = 0; index < static_cast<int32>(ESOLHudFont::Count); ++index)
    {
        const ESOLHudFont font = static_cast<ESOLHudFont>(index);
        if (const UFont* engineFont = GetHudFont(font))
        {
            mShapeLayer->SetFont(font, engineFont->GetLegacySlateFontInfo());
            mMenuLayer->SetFont(font, engineFont->GetLegacySlateFontInfo());
        }
    }
    viewport->AddViewportWidgetContent(mShapeLayer.ToSharedRef(), HUD_SHAPE_LAYER_Z_ORDER);
    viewport->AddViewportWidgetContent(mMenuLayer.ToSharedRef(), HUD_MENU_LAYER_Z_ORDER);
    mIsMenuFrameStamped = false;
    mShapeStrength = -1.0f;
    mShapeComposite = -1.0f;
    mShapeGlowScale = -1.0f;
    mShapeViewSize = FVector2f::ZeroVector;
}

//////////////////////////////////////////////////////////////////////////
// Removes the shape layer widget from the game viewport
void ASOLFlightHud::DestroyShapeLayer()
{
    UWorld* world = GetWorld();
    UGameViewportClient* viewport = world != nullptr ? world->GetGameViewport() : nullptr;
    if (mShapeLayer.IsValid())
    {
        if (viewport != nullptr)
        {
            viewport->RemoveViewportWidgetContent(mShapeLayer.ToSharedRef());
        }
        mShapeLayer.Reset();
    }
    if (mMenuLayer.IsValid())
    {
        if (viewport != nullptr)
        {
            viewport->RemoveViewportWidgetContent(mMenuLayer.ToSharedRef());
        }
        mMenuLayer.Reset();
    }
    mIsMenuFrameStamped = false;
    ShapeMaterial = nullptr;
    HoloMenu = nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Starts this frame's shape batch and text lines and pushes changed material parameters (strength, composite, size)
void ASOLFlightHud::BeginShapeFrame()
{
    mWasShapeRendererActive = false;
    mBatch = nullptr;
    mTextList = nullptr;
    // A card's content left open last frame (a missed EndCardContent) must not send this frame's HUD to the menu layer
    mCardHudBatch = nullptr;
    mCardHudTextList = nullptr;
    mContentCard = ESOLHudCard::Count;
    mReadoutCard = ESOLHudCard::Count;
    mGlowPadPx = SOL::Hologram::HUD_GLOW_PAD_PX;
    mLastPanelCount = 0;
    mWasSteerHintDrawn = false;

    // The HUD glow scale g (SDD 37 section 4.4) from the live value, so the Settings page previews at once; the route
    // overlay reads it too, with or without the shape renderer
    mHudGlowScale = SOLHudShapeMath::HudGlowScale(Settings != nullptr ? Settings->GetLiveValues().HudGlow
        : SOL::Settings::HUD_GLOW_DEFAULT);
    if (!mShapeLayer.IsValid())
    {
        return;
    }

    // Warn once if the previous frame overflowed a fixed capacity (lines, shapes or backings silently disappear)
    const int32 droppedShapes = mShapeLayer->GetBatch().GetDroppedCount()
        + (mMenuLayer.IsValid() ? mMenuLayer->GetBatch().GetDroppedCount() : 0);
    const int32 droppedTexts = mShapeLayer->GetTexts().GetDroppedCount()
        + (mMenuLayer.IsValid() ? mMenuLayer->GetTexts().GetDroppedCount() : 0);
    if (!mIsOverflowWarned && (droppedShapes > 0 || droppedTexts > 0 || mWasBackingDropped))
    {
        mIsOverflowWarned = true;
        UE_LOG(LogSOL, Warning, TEXT("FlightHud %s: HUD capacity overflow (%d shapes, %d text lines dropped, "
            "backings %s); raise the HUD_MAX_* limits in SOLConstants.h"), *GetName(), droppedShapes, droppedTexts,
            mWasBackingDropped ? TEXT("dropped") : TEXT("fit"));
    }
    mWasBackingDropped = false;
    mShapeLayer->ResetFrame();
    mShapeLayer->MarkBatchFrame(GFrameCounter);
    BeginMenuFrame();
    if (ShapeMaterial == nullptr || Canvas == nullptr)
    {
        return;
    }
    mBatch = GetActiveShapeBatch();
    mTextList = mBatch != nullptr ? &mShapeLayer->GetTexts() : nullptr;
    mWasShapeRendererActive = mBatch != nullptr;
    mShapeLayer->SetTextGlow(CVarHudTextGlow.GetValueOnGameThread() != 0);
    if (mMenuLayer.IsValid())
    {
        mMenuLayer->SetTextGlow(CVarHudTextGlow.GetValueOnGameThread() != 0);
    }

    // Parameters change rarely (Settings, the cvar, a resize): pushed only when they differ
    const float strength = Settings != nullptr ? Settings->GetLiveValues().HudEffectsStrength
        : SOL::Settings::HUD_EFFECTS_DEFAULT;
    const float composite = CVarHudComposite.GetValueOnGameThread() != 0 ? 1.0f : 0.0f;
    const FVector2f viewSize(Canvas->ClipX, Canvas->ClipY);
    if (strength != mShapeStrength)
    {
        mShapeStrength = strength;
        ShapeMaterial->SetScalarParameterValue(SOL::HudShapeMaterialParams::EFFECTS_STRENGTH, strength);
    }
    if (composite != mShapeComposite)
    {
        mShapeComposite = composite;
        ShapeMaterial->SetScalarParameterValue(SOL::HudShapeMaterialParams::COMPOSITE, composite);
    }
    if (mHudGlowScale != mShapeGlowScale)
    {
        // The HUD layer only: the menu layer's MID keeps the material's GlowScale 1 (SDD 37 D3)
        mShapeGlowScale = mHudGlowScale;
        ShapeMaterial->SetScalarParameterValue(SOL::HudShapeMaterialParams::GLOW_SCALE, mHudGlowScale);
        mShapeLayer->SetTextGlowAlpha(SOLHudShapeMath::TextGlowAlpha(mHudGlowScale));
    }
    if (viewSize != mShapeViewSize)
    {
        mShapeViewSize = viewSize;
        ShapeMaterial->SetScalarParameterValue(SOL::HudShapeMaterialParams::VIEW_WIDTH, viewSize.X);
        ShapeMaterial->SetScalarParameterValue(SOL::HudShapeMaterialParams::VIEW_HEIGHT, viewSize.Y);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the shape batch while the shape renderer is active, else nullptr
FSOLHudShapeBatch* ASOLFlightHud::GetActiveShapeBatch()
{
    return mShapeLayer.IsValid() && ShapeMaterial != nullptr && CVarHudRenderer.GetValueOnGameThread() != 0
        ? &mShapeLayer->GetBatch() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Starts this frame's menu layer: emptied only if the last frame drew on it, stamped only when written to, so a frame
// with no menu or card costs nothing there; binds the menu material and feeds the subsystem's per-frame update
void ASOLFlightHud::BeginMenuFrame()
{
    mIsMenuFrameStamped = false;
    if (!mMenuLayer.IsValid())
    {
        return;
    }
    if (mMenuLayer->GetBatch().GetShapeCount() > 0 || mMenuLayer->GetBackingCount() > 0
        || mMenuLayer->GetBatch().GetDroppedCount() > 0 || mMenuLayer->GetTexts().GetItems().Num() > 0
        || mMenuLayer->GetTexts().GetDroppedCount() > 0)
    {
        mMenuLayer->ResetFrame();
    }
    // A readout's glow override left set by a missed EndReadoutGlow (on a frame that drew nothing) ends here too
    mMenuLayer->GetBatch().ClearGlowOverride();
    mMenuLayer->GetTexts().ClearGlowAlphaOverride();
    if (HoloMenu == nullptr)
    {
        return;
    }
    UMaterialInstanceDynamic* menuMaterial = HoloMenu->GetMenuMaterial();
    if (mMenuLayer->GetMaterial() != menuMaterial)
    {
        mMenuLayer->SetMaterial(menuMaterial);
    }
    HoloMenu->UpdateMaterialForFrame();
}

//////////////////////////////////////////////////////////////////////////
// Returns the batch menu shapes go to this frame (the menu layer's, stamped on first use; else the HUD's; else none)
FSOLHudShapeBatch* ASOLFlightHud::GetMenuBatch()
{
    if (mBatch == nullptr)
    {
        return nullptr;
    }
    if (!mMenuLayer.IsValid() || mMenuLayer->GetMaterial() == nullptr)
    {
        return mBatch;
    }
    if (!mIsMenuFrameStamped)
    {
        mIsMenuFrameStamped = true;
        mMenuLayer->MarkBatchFrame(GFrameCounter);
    }
    return &mMenuLayer->GetBatch();
}

//////////////////////////////////////////////////////////////////////////
// Draws a menu text line on the menu layer, above its backing (else as DrawBuffer: HUD layer or Canvas); returns its
// rectangle
FBox2D ASOLFlightHud::MenuText(const ESOLHudLine line, const FLinearColor& color, const float x, const float y,
    const ESOLHudFont font, const float textScale)
{
    const FSOLHudShapeBatch* batch = GetMenuBatch();
    if (batch == nullptr || batch == mBatch)
    {
        return DrawBuffer(line, color, x, y, font, textScale);
    }
    const FVector2D size = MeasureLine(line, font);
    const float scale = mUiScale * textScale;
    mMenuLayer->GetTexts().Add(mLines[static_cast<int32>(line)].Text, FVector2f(x, y), scale, color, font);
    return FBox2D(FVector2D(x, y), FVector2D(x + size.X * scale, y + size.Y * scale));
}

//////////////////////////////////////////////////////////////////////////
// Draws a menu text line centred on centerX on the menu layer (as MenuText); returns its rectangle
FBox2D ASOLFlightHud::MenuTextCentered(const ESOLHudLine line, const FLinearColor& color, const float centerX,
    const float y, const ESOLHudFont font)
{
    const float textWidth = static_cast<float>(MeasureLine(line, font).X);
    return MenuText(line, color, centerX - 0.5f * textWidth * mUiScale, y, font);
}

//////////////////////////////////////////////////////////////////////////
// Draws a menu glow arc on the menu layer (else the HUD batch); false when dropped or with no batch
bool ASOLFlightHud::MenuArc(const FVector2f& center, const float radiusPx, const float halfWidthPx,
    const float midAngleRad, const float halfApertureRad, const FLinearColor& color, const float intensity,
    const float padPx)
{
    FSOLHudShapeBatch* batch = GetMenuBatch();
    return batch != nullptr && batch->AddArc(center, radiusPx, halfWidthPx, midAngleRad, halfApertureRad, color,
        intensity, padPx);
}

//////////////////////////////////////////////////////////////////////////
// Draws a built frame or card on the menu layer (else the HUD layer): its backing under the shapes, then the shapes
int32 ASOLFlightHud::MenuHoloShapes(const FSOLHoloFrameOutput& output)
{
    FSOLHudShapeBatch* batch = GetMenuBatch();
    if (batch == nullptr)
    {
        return 0;
    }
    SSOLHudShapeLayer* layer = batch == mBatch ? mShapeLayer.Get() : mMenuLayer.Get();
    if (output.bHasBacking && layer != nullptr)
    {
        mWasBackingDropped |= !layer->AddBacking(output.Backing.Rect, output.Backing.CornerPx,
            output.Backing.FeatherPx, SOLHoloFrameBuild::BackingOpacity(output.Backing.Alpha),
            SOL::HoloMenu::BACKDROP_TINT);
    }
    return SOLHoloFrameBuild::AppendToBatch(output, *batch);
}

//////////////////////////////////////////////////////////////////////////
// Sends the HUD's draw helpers to the menu layer (an always-on card's content above its backing); false when unused
bool ASOLFlightHud::BeginCardContent(const ESOLHudCard card)
{
    FSOLHudShapeBatch* menuBatch = mCardHudBatch == nullptr ? GetMenuBatch() : nullptr;
    if (menuBatch == nullptr || menuBatch == mBatch || !mMenuLayer.IsValid())
    {
        return false;
    }
    mCardHudBatch = mBatch;
    mCardHudTextList = mTextList;
    mBatch = menuBatch;
    mTextList = &mMenuLayer->GetTexts();
    mContentCard = card;
    mContentShapeStart = menuBatch->GetShapeCount();
    mContentTextStart = mTextList->GetItems().Num();
    // The content lines follow the hologram menu strength as the card's ticks do (flat at 0, D18); text keeps its glow.
    // Their glow is the HUD's (SDD 37 D10)
    mGlowPadPx = SOLHoloFrameBuild::LinePad(mCardStrength);
    BeginReadoutGlow(card);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Sends the HUD's draw helpers back to the HUD layer and records the content added into the card's stats
void ASOLFlightHud::EndCardContent()
{
    if (mCardHudBatch == nullptr)
    {
        return;
    }
    if (mContentCard != ESOLHudCard::Count)
    {
        FSOLHudCardStats& stats = mCardStats[static_cast<int32>(mContentCard)];
        stats.ContentShapes += mBatch->GetShapeCount() - mContentShapeStart;
        stats.ContentTexts += mTextList->GetItems().Num() - mContentTextStart;
        stats.ContentPadPx = mGlowPadPx;
    }
    EndReadoutGlow();
    mBatch = mCardHudBatch;
    mTextList = mCardHudTextList;
    mCardHudBatch = nullptr;
    mCardHudTextList = nullptr;
    mContentCard = ESOLHudCard::Count;
    mGlowPadPx = SOL::Hologram::HUD_GLOW_PAD_PX;
}

//////////////////////////////////////////////////////////////////////////
// Gives a card readout on the menu layer the HUD glow until EndReadoutGlow (nothing without the menu layer)
void ASOLFlightHud::BeginReadoutGlow(const ESOLHudCard card)
{
    mReadoutCard = ESOLHudCard::Count;
    if (mBatch == nullptr || !mMenuLayer.IsValid() || mMenuLayer->GetMaterial() == nullptr)
    {
        return;
    }
    FSOLHudShapeBatch& batch = mMenuLayer->GetBatch();
    FSOLHudTextList& texts = mMenuLayer->GetTexts();
    batch.SetGlowOverride(mHudGlowScale);
    texts.SetGlowAlphaOverride(SOLHudShapeMath::TextGlowAlpha(mHudGlowScale));
    mReadoutCard = card;
    mReadoutShapeStart = batch.GetShapeCount();
    mReadoutTextStart = texts.Num();
}

//////////////////////////////////////////////////////////////////////////
// Ends a readout's HUD glow (the next menu shapes and lines use the menu material's) and counts what it drew
void ASOLFlightHud::EndReadoutGlow()
{
    if (mReadoutCard == ESOLHudCard::Count || !mMenuLayer.IsValid())
    {
        mReadoutCard = ESOLHudCard::Count;
        return;
    }
    FSOLHudShapeBatch& batch = mMenuLayer->GetBatch();
    FSOLHudTextList& texts = mMenuLayer->GetTexts();
    batch.ClearGlowOverride();
    texts.ClearGlowAlphaOverride();
    FSOLHudCardStats& stats = mCardStats[static_cast<int32>(mReadoutCard)];
    stats.ReadoutShapes += batch.GetShapeCount() - mReadoutShapeStart;
    stats.ReadoutTexts += texts.Num() - mReadoutTextStart;
    mReadoutCard = ESOLHudCard::Count;
}

//////////////////////////////////////////////////////////////////////////
// Builds a frameless card around a content rectangle at a look, draws it on the menu layer and records what it drew
void ASOLFlightHud::DrawCard(const ESOLHudCard card, const FBox2D& content, const FSOLHoloMenuLook& look)
{
    if (content.bIsValid && look.bVisible)
    {
        AddHudAvoidBox(content.ExpandBy(SOL::Hologram::HUD_PANEL_PADDING_PX * mUiScale));
    }
    if (mBatch == nullptr || mCardHudBatch != nullptr || !content.bIsValid || !look.bVisible)
    {
        return;
    }
    const FBox2f contentRect(FVector2f(content.Min), FVector2f(content.Max));
    SOLHoloFrameBuild::BuildCard(contentRect, mUiScale, look, mCardStrength, mCardOutput);
    const FSOLHudShapeBatch* batch = GetMenuBatch();

    // What it drew, for the smokes: its parts, its tint and its rect (as BuildCard grows the content)
    FSOLHudCardStats& stats = mCardStats[static_cast<int32>(card)];
    stats.bOnMenuLayer = batch != nullptr && batch != mBatch;
    stats.FrameShapeStart = stats.bOnMenuLayer ? batch->GetShapeCount() : -1;
    stats.Shapes = MenuHoloShapes(mCardOutput);
    stats.bDrawn = true;
    stats.Look = look;
    stats.Strength = mCardStrength;
    stats.BackingAlpha = mCardOutput.bHasBacking ? mCardOutput.Backing.Alpha : -1.0f;
    for (int32 index = 0; index < mCardOutput.ShapeCount; ++index)
    {
        const FSOLHoloShape& shape = mCardOutput.Shapes[index];
        if (shape.Part == ESOLHoloPart::CardTick)
        {
            ++stats.Ticks;
            stats.TickPadPx = shape.PadPx;
        }
        else if (shape.Part == ESOLHoloPart::Sweep)
        {
            ++stats.Sweeps;
        }
        else if (shape.Part != ESOLHoloPart::Fill)
        {
            ++stats.Others;
        }
    }
    FVector2f center;
    FVector2f halfSize;
    SOLHudShapeMath::PanelAroundRect(contentRect, SOL::Hologram::HUD_PANEL_PADDING_PX * mUiScale, center, halfSize);
    stats.Rect = FBox2f(center - halfSize, center + halfSize);
}

//////////////////////////////////////////////////////////////////////////
// Returns a card's look this frame from its anim (whole and plain under the Canvas fallback while live)
FSOLHoloMenuLook ASOLFlightHud::CardLook(const FSOLHoloMenuAnim& anim, const bool bLive) const
{
    if (mBatch == nullptr)
    {
        // The Canvas fallback draws a live card whole, as before (no effects), and a gone one not at all
        FSOLHoloMenuLook look;
        look.bVisible = bLive;
        look.Progress = bLive ? 1.0f : 0.0f;
        look.FrameFraction = look.Progress;
        look.FillAlpha = look.Progress;
        return look;
    }
    return SOLHoloMenu::Evaluate(anim, mCardNowS, mCardStrength);
}

//////////////////////////////////////////////////////////////////////////
// Returns what a card drew last frame
const ASOLFlightHud::FSOLHudCardStats& ASOLFlightHud::GetLastCard(const ESOLHudCard card) const
{
    return mCardStats[FMath::Clamp(static_cast<int32>(card), 0, static_cast<int32>(ESOLHudCard::Count) - 1)];
}

//////////////////////////////////////////////////////////////////////////
// Returns how many of the HUD layer's backings this frame overlap a rectangle
int32 ASOLFlightHud::CountHudBackingsOverlapping(const FBox2f& rect) const
{
    if (!mShapeLayer.IsValid() || !rect.bIsValid)
    {
        return 0;
    }
    int32 count = 0;
    for (int32 index = 0; index < mShapeLayer->GetBackingCount(); ++index)
    {
        count += mShapeLayer->GetBackingRect(index).Intersect(rect) ? 1 : 0;
    }
    return count;
}

//////////////////////////////////////////////////////////////////////////
// Returns the text lines the menu layer's last paint drew
int32 ASOLFlightHud::GetLastMenuPaintedTextCount() const
{
    return mMenuLayer.IsValid() ? mMenuLayer->GetLastPaintedTextCount() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the backings the menu layer's last paint drew
int32 ASOLFlightHud::GetLastMenuPaintedBackingCount() const
{
    return mMenuLayer.IsValid() ? mMenuLayer->GetLastPaintedBackingCount() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns true when the bus holds a pilot snapshot published within the last few frames
bool ASOLFlightHud::HasFreshPilot() const
{
    return HudEvents != nullptr && HudEvents->GetBus().HasPilot()
        && GFrameCounter <= HudEvents->GetBus().GetPilotFrame() + SOL::Hologram::HUD_PILOT_MAX_FRAME_LAG;
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of glow shapes the last frame queued
int32 ASOLFlightHud::GetLastShapeCount() const
{
    return mShapeLayer.IsValid() ? mShapeLayer->GetBatch().GetShapeCount() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the HUD layer's glow-text outline alpha (-1 without a shape layer)
float ASOLFlightHud::GetTextGlowAlpha() const
{
    return mShapeLayer.IsValid() ? mShapeLayer->GetTextGlowAlpha() : -1.0f;
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of glow-text lines the last frame queued
int32 ASOLFlightHud::GetLastTextCount() const
{
    return mShapeLayer.IsValid() ? mShapeLayer->GetTexts().Num() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of glow-text lines the shape layer's last paint actually drew
int32 ASOLFlightHud::GetLastPaintedTextCount() const
{
    return mShapeLayer.IsValid() ? mShapeLayer->GetLastPaintedTextCount() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of panel backings the shape layer's last paint actually drew
int32 ASOLFlightHud::GetLastPaintedBackingCount() const
{
    return mShapeLayer.IsValid() ? mShapeLayer->GetLastPaintedBackingCount() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the material the menu layer draws with, or null
UObject* ASOLFlightHud::GetMenuLayerMaterial() const
{
    return mMenuLayer.IsValid() ? mMenuLayer->GetMaterial() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Returns the shapes queued on the menu layer this frame
int32 ASOLFlightHud::GetLastMenuShapeCount() const
{
    return mMenuLayer.IsValid() ? mMenuLayer->GetBatch().GetShapeCount() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the backings queued on the menu layer this frame
int32 ASOLFlightHud::GetLastMenuBackingCount() const
{
    return mMenuLayer.IsValid() ? mMenuLayer->GetBackingCount() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the shapes the menu layer's last paint drew
int32 ASOLFlightHud::GetLastMenuPaintedShapeCount() const
{
    return mMenuLayer.IsValid() ? mMenuLayer->GetLastPaintedShapeCount() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the menu layer's shape batch as last filled, or null without a menu layer
const FSOLHudShapeBatch* ASOLFlightHud::GetMenuLayerBatch() const
{
    return mMenuLayer.IsValid() ? &mMenuLayer->GetBatch() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Returns the menu layer's text lines as last filled, or null without a menu layer
const FSOLHudTextList* ASOLFlightHud::GetMenuLayerTexts() const
{
    return mMenuLayer.IsValid() ? &mMenuLayer->GetTexts() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Returns the frame the menu layer was last stamped for (0: never)
uint64 ASOLFlightHud::GetMenuLayerBatchFrame() const
{
    return mMenuLayer.IsValid() ? mMenuLayer->GetBatchFrame() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the engine font of a HUD font slot
UFont* ASOLFlightHud::GetHudFont(const ESOLHudFont font)
{
    if (GEngine == nullptr)
    {
        return nullptr;
    }
    switch (font)
    {
    case ESOLHudFont::Large:
        return GEngine->GetLargeFont();
    case ESOLHudFont::Medium:
        return GEngine->GetMediumFont();
    default:
        return GEngine->GetSmallFont();
    }
}

//////////////////////////////////////////////////////////////////////////
// Draws a glow line (shape renderer) or a Canvas line in colour x intensity
void ASOLFlightHud::GlowLine(const FVector2D& a, const FVector2D& b, const FLinearColor& color, const float intensity,
    const float thickness)
{
    if (mBatch != nullptr)
    {
        // Canvas widths up to 2 px become LT's 1 px glow line; thicker ones keep their extra width in the core
        const float halfWidth = SOLHudSymbols::LineHalfWidthPx(thickness);
        mBatch->AddLine(FVector2f(a), FVector2f(b), halfWidth, color, intensity, mGlowPadPx);
        return;
    }
    DrawLine(a.X, a.Y, b.X, b.Y, HudScaled(color, intensity), thickness);
}

//////////////////////////////////////////////////////////////////////////
// Draws a glow ring (shape renderer) or a Canvas circle of line segments in colour x intensity
void ASOLFlightHud::GlowRing(const FVector2D& center, const float radiusPx, const FLinearColor& color,
    const float intensity, const float thickness, const int32 segments)
{
    if (mBatch != nullptr)
    {
        mBatch->AddRing(FVector2f(center), radiusPx, SOL::Hologram::HUD_LINE_HALF_WIDTH_PX, color, intensity,
            mGlowPadPx);
        return;
    }
    DrawCircle(center, radiusPx, HudScaled(color, intensity), segments, 1.0f, thickness);
}

//////////////////////////////////////////////////////////////////////////
// Draws a filled glow box (a capsule as tall as the box) or a Canvas rectangle
void ASOLFlightHud::GlowBox(const float x, const float y, const float width, const float height,
    const FLinearColor& color, const float intensity)
{
    if (width <= 0.0f || height <= 0.0f)
    {
        return;
    }
    if (mBatch != nullptr)
    {
        FVector2f start;
        FVector2f end;
        float halfWidth = 0.0f;
        if (SOLHudShapeMath::BoxCapsule(x, y, width, height, SOL::Hologram::HUD_LINE_HALF_WIDTH_PX, start, end,
            halfWidth))
        {
            mBatch->AddLine(start, end, halfWidth, color, intensity, mGlowPadPx);
        }
        return;
    }
    DrawRect(HudScaled(color, intensity), x, y, width, height);
}

//////////////////////////////////////////////////////////////////////////
// Draws an axis-aligned glow ellipse ring (shape renderer) or a Canvas ellipse of line segments
void ASOLFlightHud::GlowEllipse(const FVector2D& center, const float radiusXPx, const float squashY,
    const FLinearColor& color, const float intensity)
{
    if (mBatch != nullptr)
    {
        mBatch->AddEllipse(FVector2f(center), radiusXPx, radiusXPx * squashY, SOL::Hologram::HUD_LINE_HALF_WIDTH_PX,
            color, intensity, mGlowPadPx);
        return;
    }
    DrawCircle(center, radiusXPx, HudScaled(color, intensity), SOL::HudGeometry::CIRCLE_SEGMENTS, squashY, 1.0f);
}

//////////////////////////////////////////////////////////////////////////
// Draws a dashed glow line from a to b, dashes starting at a (one dashed-line shape, or Canvas dashes)
void ASOLFlightHud::GlowDashedLine(const FVector2D& a, const FVector2D& b, const double dashPx, const double gapPx,
    const FLinearColor& color, const float intensity)
{
    if (mBatch != nullptr)
    {
        // One shape: separate dash quads would each add the whole line's halo (a white column)
        mBatch->AddDashedLine(FVector2f(a), FVector2f(b), SOL::Hologram::HUD_LINE_HALF_WIDTH_PX,
            static_cast<float>(dashPx + gapPx), color, intensity, mGlowPadPx);
        return;
    }
    const double lengthPx = FVector2D::Distance(a, b);
    const FVector2D dir = lengthPx > 0.0 ? (b - a) / lengthPx : FVector2D::ZeroVector;
    const int32 dashes = SOLRadar::DashCount(lengthPx, dashPx, gapPx);
    for (int32 dash = 0; dash < dashes; ++dash)
    {
        double startPx = 0.0;
        double endPx = 0.0;
        SOLRadar::DashSegment(lengthPx, dashPx, gapPx, dash, startPx, endPx);
        DrawLine(a.X + dir.X * startPx, a.Y + dir.Y * startPx, a.X + dir.X * endPx, a.Y + dir.Y * endPx,
            HudScaled(color, intensity), 1.0f);
    }
}

//////////////////////////////////////////////////////////////////////////
// Draws a filled glow dot (shape renderer) or a Canvas square of the same size
void ASOLFlightHud::GlowDot(const FVector2D& center, const float radiusPx, const FLinearColor& color,
    const float intensity)
{
    if (mBatch != nullptr)
    {
        mBatch->AddLine(FVector2f(center), FVector2f(center), radiusPx, color, intensity, mGlowPadPx);
        return;
    }
    DrawRect(HudScaled(color, intensity), center.X - radiusPx, center.Y - radiusPx, 2.0f * radiusPx, 2.0f * radiusPx);
}

//////////////////////////////////////////////////////////////////////////
// Draws a fill bar: a faint track (the Canvas fallback a dark back instead), the filled part (clamped to [0, 1]) in a
// palette role and an end tick
void ASOLFlightHud::DrawBar(const float x, const float y, const double fill01, const ESOLHudPaletteRole fillRole)
{
    if (mBatch == nullptr)
    {
        DrawRect(HUD_BAR_BACK_COLOR, x, y, SOL::HudGeometry::SPEED_BAR_WIDTH_PX * mUiScale,
            SOL::HudGeometry::SPEED_BAR_HEIGHT_PX * mUiScale);
    }
    mSymbolScratch.Reset();
    SOLHudSymbols::AppendBar(x, y, fill01, fillRole, mUiScale, mSymbolScratch);
    ReplaySymbolPrims(mSymbolScratch);
}

//////////////////////////////////////////////////////////////////////////
// Replays symbol primitives through the Glow* helpers and MenuArc in order (ShapeOnly ones only with the shape
// renderer); returns the primitives drawn (an Arc counts only when MenuArc took it)
int32 ASOLFlightHud::ReplaySymbolPrims(const FSOLHudSymbolPrims& prims)
{
    // Every HUD builder call fits the capacity (SOL::Help::MAX_PRIMS_PER_SYMBOL); a drop is a builder bug
    ensure(prims.DroppedCount == 0);
    int32 drawn = 0;
    for (int32 index = 0; index < prims.Count; ++index)
    {
        const FSOLHudSymbolPrim& prim = prims.Prims[index];
        if (prim.Path == ESOLHudPrimPath::ShapeOnly && mBatch == nullptr)
        {
            continue;
        }
        const FLinearColor& color = HudRoleColor(prim.Role);

        // A primitive's own pad replaces the Glow* helpers' pad for this primitive (MenuArc takes PadPx itself)
        TGuardValue<float> padGuard(mGlowPadPx,
            prim.Kind != ESOLHudPrimKind::Arc && prim.PadPx > 0.0f ? prim.PadPx : mGlowPadPx);
        switch (prim.Kind)
        {
        case ESOLHudPrimKind::Line:
            GlowLine(FVector2D(prim.A), FVector2D(prim.B), color, prim.Intensity, prim.ThicknessPx);
            break;
        case ESOLHudPrimKind::Ring:
            GlowRing(FVector2D(prim.A), prim.RadiusPx, color, prim.Intensity, prim.ThicknessPx, prim.Segments);
            break;
        case ESOLHudPrimKind::Box:
            GlowBox(prim.A.X, prim.A.Y, prim.B.X, prim.B.Y, color, prim.Intensity);
            break;
        case ESOLHudPrimKind::Ellipse:
            GlowEllipse(FVector2D(prim.A), prim.RadiusPx, prim.SquashY, color, prim.Intensity);
            break;
        case ESOLHudPrimKind::DashedLine:
            GlowDashedLine(FVector2D(prim.A), FVector2D(prim.B), prim.DashPx, prim.GapPx, color, prim.Intensity);
            break;
        case ESOLHudPrimKind::Dot:
            GlowDot(FVector2D(prim.A), prim.RadiusPx, color, prim.Intensity);
            break;
        case ESOLHudPrimKind::Arc:
            if (!MenuArc(prim.A, prim.RadiusPx, prim.HalfWidthPx, prim.MidAngleRad, prim.HalfApertureRad, color,
                prim.Intensity, prim.PadPx))
            {
                continue;
            }
            break;
        default:
            // Panels, icons, cards, frames and text come from the glossary's Build, never from a HUD builder
            ensureMsgf(false, TEXT("ReplaySymbolPrims: primitive kind %d is not a HUD primitive"),
                static_cast<int32>(prim.Kind));
            continue;
        }
        ++drawn;
    }
    return drawn;
}

//////////////////////////////////////////////////////////////////////////
// Queues a hologram panel (glow shape and dark backing, scaled by backingOpacity) around a content rectangle
void ASOLFlightHud::AddHoloPanel(const FBox2D& content, const FLinearColor& color, const float backingOpacity)
{
    if (content.bIsValid && color.A > 0.0f)
    {
        AddHudAvoidBox(content.ExpandBy(SOL::Hologram::HUD_PANEL_PADDING_PX * mUiScale));
    }
    if (mBatch == nullptr || !content.bIsValid || !mShapeLayer.IsValid())
    {
        return;
    }
    FVector2f center;
    FVector2f halfSize;
    SOLHudShapeMath::PanelAroundRect(FBox2f(FVector2f(content.Min), FVector2f(content.Max)),
        SOL::Hologram::HUD_PANEL_PADDING_PX * mUiScale, center, halfSize);
    if (mBatch->AddPanel(center, halfSize, color, SOL::Hologram::HUD_PANEL_INTENSITY,
        SOL::Hologram::HUD_PANEL_FILL, SOL::Hologram::HUD_PANEL_QUAD_PAD_PX))
    {
        mWasBackingDropped |= !mShapeLayer->AddBacking(FBox2f(center - halfSize, center + halfSize),
            static_cast<float>(SOL::Hologram::HUD_PANEL_CORNER_PX) * mUiScale,
            SOL::Hologram::HUD_PANEL_BACKING_FEATHER_PX * mUiScale, backingOpacity);
        ++mLastPanelCount;
    }
}

//////////////////////////////////////////////////////////////////////////
// Rebuilds the cached key hint lines from the applied key bindings
void ASOLFlightHud::RebuildKeyHints()
{
    const TArray<FString> keys = Settings != nullptr ? Settings->GetAppliedKeys() : SOLKeyBindings::DefaultKeys();
    mFlightHintsOrbitsOn = SOLHudFormat::FormatFlightKeyHints(keys, true);
    mFlightHintsOrbitsOff = SOLHudFormat::FormatFlightKeyHints(keys, false);
    mMapHints = SOLHudFormat::FormatMapKeyHints(keys);
    mKeyTexts = SOLHudFormat::FormatHudKeyTexts(keys);
}

//////////////////////////////////////////////////////////////////////////
// Returns true while the jump map is open (the view is the map camera, not the ship's)
bool ASOLFlightHud::IsMapOpen() const
{
    return MapMode != nullptr && MapMode->IsMapOpen();
}

//////////////////////////////////////////////////////////////////////////
// Toggles the world-space orbit lines (O), owned by ASOLOrbitLineVisuals (SDD 17)
void ASOLFlightHud::ToggleOrbitLines()
{
    if (ASOLOrbitLineVisuals* lines = GetOrbitLineVisuals())
    {
        lines->ToggleLines();
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns true while the world-space orbit lines are shown
bool ASOLFlightHud::AreOrbitLinesVisible() const
{
    const ASOLOrbitLineVisuals* lines = GetOrbitLineVisuals();
    return lines != nullptr && lines->AreLinesEnabled();
}

//////////////////////////////////////////////////////////////////////////
// Returns the game mode's orbit-line actor, or nullptr
ASOLOrbitLineVisuals* ASOLFlightHud::GetOrbitLineVisuals() const
{
    const ASOLGameMode* gameMode = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<ASOLGameMode>() : nullptr;
    return gameMode != nullptr ? gameMode->GetOrbitLineVisuals() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Steps the radar range to the next power of ten in ('-') or out ('='); switches to MANUAL only when the range changes
void ASOLFlightHud::StepRadarZoom(const bool bZoomIn)
{
    // The first press starts from the range on screen; later presses from the manual range
    const double currentM = mIsRadarManual ? mManualRadarRangeM : mLastRadarRangeM;
    const double steppedM = SOLRadar::StepManualRange(currentM, bZoomIn);

    // A press that leaves the range where it is (e.g. '-' at the 1 km floor) keeps the current mode
    if (steppedM == currentM)
    {
        UE_LOG(LogSOL, Log, TEXT("FlightHud %s: radar zoom %s is a no-op at %.4g m (%s kept)"), *GetName(),
            bZoomIn ? TEXT("in") : TEXT("out"), currentM, mIsRadarManual ? TEXT("MANUAL") : TEXT("AUTO"));
        return;
    }
    mManualRadarRangeM = steppedM;
    mIsRadarManual = true;
    UE_LOG(LogSOL, Log, TEXT("FlightHud %s: radar zoom %s -> MANUAL %.4g m"), *GetName(), bZoomIn ? TEXT("in")
        : TEXT("out"), mManualRadarRangeM);
}

//////////////////////////////////////////////////////////////////////////
// Returns the radar to AUTO range (Home)
void ASOLFlightHud::ResetRadarAuto()
{
    mIsRadarManual = false;
    UE_LOG(LogSOL, Log, TEXT("FlightHud %s: radar range AUTO"), *GetName());
}

//////////////////////////////////////////////////////////////////////////
// Draws every HUD element for this frame
void ASOLFlightHud::DrawHUD()
{
    Super::DrawHUD();
    mLastWarpStreaksDrawn = 0;
    mWasFlightHudDrawn = false;
    mLastProfileTagAlpha = 0.0f;
    mLastSurfaceLockLine = ESOLSurfaceLockHudLine::None;
    mLastRouteLineText.Reset();
    mLastRouteLineRect = FBox2D(ForceInit);
    mLastHintsRect = FBox2D(ForceInit);
    mAutopilotBlockBottomY = 0.0f;
    mLeftBlockRect = FBox2D(ForceInit);
    mTopBlockRect = FBox2D(ForceInit);

    // Last frame's HUD panel boxes become the set the lane labels avoid (the labels draw before the panels)
    for (int32 index = 0; index < mHudAvoidBoxCount; ++index)
    {
        mPrevHudAvoidBoxes[index] = mHudAvoidBoxes[index];
    }
    mPrevHudAvoidBoxCount = mHudAvoidBoxCount;
    mHudAvoidBoxCount = 0;
    mRouteOverlay.ResetFrame();
    mHologramOverlay.ResetFrame();
    mIconLayer.ClearStats();
    mLastRadarShipCount = 0;
    mLastRadialSectorCount = 0;
    mLastInfoCardLineCount = 0;
    if (Interaction != nullptr)
    {
        // A frame that draws no icons leaves nothing to hover or press
        Interaction->ClearIconHits();
    }

    // The glow-shape batch is emptied first, so a frame that returns early below leaves nothing on screen
    BeginShapeFrame();

    // The cards (SDD 42 D15) read one real time and strength per frame; the always-on ones are Open, with no draw-in
    for (FSOLHudCardStats& stats : mCardStats)
    {
        stats = FSOLHudCardStats();
    }
    mCardNowS = FPlatformTime::Seconds();
    mCardStrength = HoloMenu != nullptr ? HoloMenu->GetStrength() : SOL::Settings::HOLO_MENU_EFFECTS_DEFAULT;
    FSOLHoloMenuAnim alwaysOnAnim;
    alwaysOnAnim.Phase = ESOLHoloMenuPhase::Open;
    mAlwaysOnCardLook = CardLook(alwaysOnAnim, true);
    if (Canvas == nullptr || SimClock == nullptr || BodyRegistry == nullptr || AnchorSubsystem == nullptr
        || GEngine == nullptr)
    {
        return;
    }
    const SOLSettingsModel::FSettingsValues* settings = Settings != nullptr ? &Settings->GetLiveValues() : nullptr;
    mUiScale = FMath::Clamp(Canvas->ClipY / SOL::Hologram::HUD_REFERENCE_HEIGHT_PX, 1.0f,
        SOL::Hologram::HUD_MAX_UI_SCALE)
        * (settings != nullptr ? settings->HudScale : 1.0f);
    const USOLMenuSubsystem* menu = GetWorld()->GetSubsystem<USOLMenuSubsystem>();
    const bool bIsMenuShowing = menu != nullptr && menu->IsMenuShowing();
    const bool bIsWarping = Jump != nullptr && Jump->IsWarping();
    const bool bIsHudHidden = settings != nullptr && !settings->bHudVisible && !IsMapOpen();

    // The radial menu's anim and cues (SDD 42) run every frame before the early returns below, so an action that
    // hides the flight HUD at its release (Jump To starts the warp) still plays its select cue on that frame; the
    // radial shows only while the flight HUD draws and the map is closed (drawn in DrawInteraction)
    UpdateRadialMenu(!bIsMenuShowing && !bIsWarping && !bIsHudHidden && !IsMapOpen());

    // The main and pause menus (SDD 8) draw over a clean view: no flight HUD while one shows
    if (bIsMenuShowing)
    {
        HideLaneTunnelLabels();
        return;
    }

    // Jump warp: the flight HUD is hidden for the whole sequence; only the radial streaks are drawn
    if (bIsWarping)
    {
        mLastWarpStreaksDrawn = DrawWarpStreaks(Jump->ComputeStreakIntensity(), Jump->GetElapsedS());
        HideLaneTunnelLabels();
        return;
    }

    // The player hid the flight HUD (SDD 8): nothing more, unless the jump map needs its overlays
    if (bIsHudHidden)
    {
        HideLaneTunnelLabels();
        return;
    }
    mWasFlightHudDrawn = true;

    FSOLHudView view;
    const bool bHasView = BuildView(view);

    // World-anchored overlays first so the panels draw on top of them
    FSOLMapOverlayView overlayView;
    overlayView.LocationCm = view.Location;
    overlayView.Rotation = view.Rotation;
    overlayView.Center = view.Center;
    overlayView.FocalPx = view.FocalPx;
    if (bHasView && IsMapOpen())
    {
        // Hologram look (SDD 20): the ecliptic grid through the camera focus and the stems, under the icons, only
        // while the proxies show (not over the real bodies if the hologram assets are missing)
        const ASOLGameMode* gameMode = GetWorld()->GetAuthGameMode<ASOLGameMode>();
        const ASOLHologramVisuals* hologram = gameMode != nullptr ? gameMode->GetHologramVisuals() : nullptr;
        if (hologram != nullptr && hologram->IsShown() && SOLHologramSwitch::IsHologramEnabled())
        {
            DrawHologramOverlay(overlayView);
        }

        // Jump map: bodies too small on screen get their icon and name label (SDD 3, sub-part 2c)
        mMapBodyOverlay.Draw(*Canvas, overlayView, *MapMode, *AnchorSubsystem, BodyRegistry->GetRegistry(),
            GEngine->GetSmallFont(), HUD_PRIMARY, mUiScale, mBatch);
    }

    // Routes and the draft (SDD 20): always on the map, in flight while the route overlay is on (P)
    if (bHasView && Routes != nullptr && (IsMapOpen() || Routes->IsOverlayVisible()))
    {
        DrawRouteOverlay(overlayView);
    }
    if (bHasView && IsMapOpen())
    {
        // Destination pick (2d): reference ring, disc, height guide line and destination marker over the icons
        mMapPickOverlay.Draw(*Canvas, overlayView, *MapMode, *AnchorSubsystem, mUiScale);
    }
    DrawOverlayIcons(bHasView && !IsMapOpen() ? &view : nullptr);
    DrawLaneTunnelLabels(bHasView && !IsMapOpen() ? &view : nullptr);
    DrawInfoBlock();
    AddHudAvoidBox(mLastHintsRect);
    DrawInteraction();

    if (Ships == nullptr || Targeting == nullptr || !Ships->HasPlayerShip())
    {
        return;
    }
    const FSOLShipState state = Ships->GetState();
    const FSOLShipControl control = Ships->GetControl();

    // One-time notice after a save from before the issue #19 key layout was migrated (SDD 19 Q11)
    const double realTimeS = GetWorld()->GetRealTimeSeconds();
    if (Settings != nullptr && Settings->ConsumeControlsChangedNotice())
    {
        mControlsNoticeEndS = realTimeS + HUD_CONTROLS_NOTICE_S;
        UE_LOG(LogSOL, Log, TEXT("FlightHud %s: default controls changed notice shown"), *GetName());
    }
    if (realTimeS < mControlsNoticeEndS)
    {
        mText.Reset();
        mText.Append(mKeyTexts.ControlsChangedNotice);
        AddHoloPanel(DrawBufferCentered(ESOLHudLine::ControlsNotice, HUD_SECONDARY, Canvas->ClipX * 0.5f,
            Canvas->ClipY * HUD_CONTROLS_NOTICE_Y_FRACTION, ESOLHudFont::Medium), HUD_SECONDARY);
    }
    DrawOverlayProfileTag(realTimeS);

    // Text blocks gather their rectangles while drawing; the speed and G block is the speed card (menu layer), the
    // autopilot and route block keeps its HUD panel
    DrawSpeedBlock(state, control);
    DrawPilotBlock(control);
    DrawRouteMarker();
    DrawCard(ESOLHudCard::Speed, mLeftBlockRect, mAlwaysOnCardLook);
    AddHoloPanel(mTopBlockRect, HUD_PRIMARY);
    DrawRadar(bHasView ? &view : nullptr, state);

    // The jump map views from its own camera: the ship-view pilot aids (reticle, joystick, velocity markers) are
    // hidden; the target bracket still projects through the active camera
    if (IsMapOpen())
    {
        if (bHasView)
        {
            DrawTarget(view, state);
        }
        return;
    }

    // Pilot aids at the screen center (joystick only when flying the ship, not the debug spectator), drawn before
    // the target bracket and velocity markers so they never paint over them
    const FVector2D center(Canvas->ClipX * 0.5, Canvas->ClipY * 0.5);
    DrawReticle(center);
    if (HasFreshPilot())
    {
        DrawJoystick(center, HudEvents->GetBus().GetPilot());
    }
    if (bHasView)
    {
        DrawVelocityMarkers(view, state);
        DrawTarget(view, state);
    }
}

//////////////////////////////////////////////////////////////////////////
// Draws the jump warp's radial streaks for a streak intensity and sequence time; returns the number drawn
int32 ASOLFlightHud::DrawWarpStreaks(const double intensity, const double elapsedS)
{
    if (intensity <= 0.0)
    {
        return 0;
    }

    // Each streak streams outward from near the center and wraps around, brightening (intensity, and the streak's
    // progress outward) as glow lines, or as Canvas lines whose RGB is scaled (Canvas lines ignore alpha)
    const FVector2D center(Canvas->ClipX * 0.5, Canvas->ClipY * 0.5);
    const double halfDiagonalPx = center.Size();
    int32 drawn = 0;
    for (const FSOLWarpStreak& streak : mWarpStreaks)
    {
        const double phase = FMath::Frac(streak.PhaseOffset + elapsedS * streak.CyclesPerSecond);
        mSymbolScratch.Reset();
        SOLHudSymbols::AppendWarpStreak(center, streak.Direction, phase, intensity, halfDiagonalPx, mUiScale,
            mSymbolScratch);
        ReplaySymbolPrims(mSymbolScratch);
        ++drawn;
    }
    return drawn;
}

//////////////////////////////////////////////////////////////////////////
// Fills the view from the player camera's cached POV for this frame; false without a camera
bool ASOLFlightHud::BuildView(FSOLHudView& outView) const
{
    if (PlayerOwner == nullptr || PlayerOwner->PlayerCameraManager == nullptr)
    {
        return false;
    }
    const FMinimalViewInfo& pov = PlayerOwner->PlayerCameraManager->GetCameraCacheView();
    const double halfFovRad = FMath::DegreesToRadians(FMath::Clamp(static_cast<double>(pov.FOV), 1.0, 170.0)) * 0.5;
    outView.Location = pov.Location;
    outView.Rotation = pov.Rotation.Quaternion();
    outView.Center = FVector2D(Canvas->ClipX * 0.5, Canvas->ClipY * 0.5);

    // Horizontal FOV across the canvas width: Config/DefaultEngine.ini sets the local player to keep the X FOV at
    // every aspect ratio (issue #25; the engine default keeps Y, which matches this only at 16:9)
    outView.FocalPx = outView.Center.X / FMath::Tan(halfFovRad);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Projects a camera-local point (X forward) to the screen; false when it is behind the near plane
bool ASOLFlightHud::ProjectLocal(const FSOLHudView& view, const FVector& localCm, FVector2D& outScreen)
{
    // A point exactly on the near plane still projects (the orbit-line clip places endpoints there)
    if (localCm.X < HUD_NEAR_PLANE_CM)
    {
        return false;
    }
    outScreen.X = view.Center.X + view.FocalPx * (localCm.Y / localCm.X);
    outScreen.Y = view.Center.Y - view.FocalPx * (localCm.Z / localCm.X);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns true when a screen point lies inside the canvas, shrunk by a margin
bool ASOLFlightHud::IsOnScreen(const FVector2D& screen, const float marginPx) const
{
    return screen.X >= marginPx && screen.Y >= marginPx && screen.X <= Canvas->ClipX - marginPx
        && screen.Y <= Canvas->ClipY - marginPx;
}

//////////////////////////////////////////////////////////////////////////
// Draws a circle (or an ellipse when squashY < 1) as line segments
void ASOLFlightHud::DrawCircle(const FVector2D& center, const float radiusPx, const FLinearColor& color,
    const int32 segments, const float squashY, const float thickness)
{
    FVector2D previous(center.X + radiusPx, center.Y);
    for (int32 segment = 1; segment <= segments; ++segment)
    {
        const double angle = UE_DOUBLE_TWO_PI * segment / segments;
        const FVector2D next(center.X + radiusPx * FMath::Cos(angle),
            center.Y + radiusPx * squashY * FMath::Sin(angle));
        DrawLine(previous.X, previous.Y, next.X, next.Y, color, thickness);
        previous = next;
    }
}

//////////////////////////////////////////////////////////////////////////
// Brings a line's cache up to date with the text buffer and font; returns the text's unscaled size
FVector2D ASOLFlightHud::MeasureLine(const ESOLHudLine line, const ESOLHudFont font)
{
    // Rebuild the FText and re-measure only when the text or font changed (StrLen's measure cache is keyed by the
    // string, so measuring a changing line every frame would copy it into that cache every frame)
    FSOLHudLineCache& cache = mLines[static_cast<int32>(line)];
    const bool bTextChanged = !cache.Source.Equals(mText, ESearchCase::CaseSensitive);
    if (bTextChanged)
    {
        cache.Source.Reset();
        cache.Source.Append(mText);
        cache.Text = FText::FromString(cache.Source);
    }
    if (bTextChanged || !cache.bMeasured || cache.Font != font)
    {
        float textWidth = 0.0f;
        float textHeight = 0.0f;
        Canvas->StrLen(GetHudFont(font), mText, textWidth, textHeight);
        cache.Size = FVector2D(textWidth, textHeight);
        cache.Font = font;
        cache.bMeasured = true;
    }
    return cache.Size;
}

//////////////////////////////////////////////////////////////////////////
// Draws the text buffer as one HUD line (glow text with the shape renderer, else Canvas); returns its rectangle
FBox2D ASOLFlightHud::DrawBuffer(const ESOLHudLine line, const FLinearColor& color, const float x, const float y,
    const ESOLHudFont font, const float textScale)
{
    const FVector2D size = MeasureLine(line, font);
    const FSOLHudLineCache& cache = mLines[static_cast<int32>(line)];
    UFont* engineFont = GetHudFont(font);
    const float scale = mUiScale * textScale;
    const FBox2D rect(FVector2D(x, y), FVector2D(x + size.X * scale, y + size.Y * scale));

    // Glow text: the FText is shared by reference count with the Slate layer, which draws it this frame
    if (mTextList != nullptr)
    {
        mTextList->Add(cache.Text, FVector2f(x, y), scale, color, font);
        return rect;
    }

    // Canvas copies the FText by reference count; its own glyph layout inside DrawItem is out of our control
    FCanvasTextItem item(FVector2D(x, y), cache.Text, engineFont, color);
    item.Scale = FVector2D(scale, scale);
    item.EnableShadow(FLinearColor::Black);
    Canvas->DrawItem(item);
    return rect;
}

//////////////////////////////////////////////////////////////////////////
// Draws the text buffer as one HUD line centred on centerX; returns its screen rectangle
FBox2D ASOLFlightHud::DrawBufferCentered(const ESOLHudLine line, const FLinearColor& color, const float centerX,
    const float y, const ESOLHudFont font)
{
    const float textWidth = static_cast<float>(MeasureLine(line, font).X);
    return DrawBuffer(line, color, centerX - 0.5f * textWidth * mUiScale, y, font);
}

//////////////////////////////////////////////////////////////////////////
// Draws the overlay profile tag (SDD 34 section 8) centred under the controls-notice line while its fade lasts
void ASOLFlightHud::DrawOverlayProfileTag(const double realTimeS)
{
    mLastProfileTagAlpha = 0.0f;
    if (mProfileTagStartS < 0.0 || mProfileTagText.IsEmpty())
    {
        return;
    }
    const float alpha = SOLOverlay::ProfileTagAlpha(realTimeS - mProfileTagStartS);
    if (alpha <= 0.0f)
    {
        return;
    }
    mLastProfileTagAlpha = alpha;

    // One line below the notice line, past both panels' padding (the notice need not be showing)
    const UFont* font = GetHudFont(ESOLHudFont::Medium);
    const float lineHeight = (font != nullptr ? font->GetMaxCharHeight() : 0.0f) * mUiScale;
    const float y = Canvas->ClipY * HUD_CONTROLS_NOTICE_Y_FRACTION + lineHeight
        + (2.0f * SOL::Hologram::HUD_PANEL_PADDING_PX + HUD_PROFILE_TAG_GAP_PX) * mUiScale;
    mText.Reset();
    mText.Append(mProfileTagText);
    const FLinearColor textColor(HUD_SECONDARY.R, HUD_SECONDARY.G, HUD_SECONDARY.B, HUD_SECONDARY.A * alpha);
    // The panel's glow and its dark backing fade with the text
    AddHoloPanel(DrawBufferCentered(ESOLHudLine::OverlayProfileTag, textColor, Canvas->ClipX * 0.5f, y,
        ESOLHudFont::Medium), HudScaled(HUD_SECONDARY, alpha), alpha);
}

//////////////////////////////////////////////////////////////////////////
// Draws the center reticle: four arms and a dimmer centre dot in LT Primary (glow shapes, or Canvas lines)
void ASOLFlightHud::DrawReticle(const FVector2D& center)
{
    // The dot ring carries the HUD glow pad itself (its PadPx), so the replay's pad does not matter here
    mSymbolScratch.Reset();
    SOLHudSymbols::AppendReticle(center, mUiScale, mSymbolScratch);
    ReplaySymbolPrims(mSymbolScratch);
    if (mBatch == nullptr)
    {
        // The Canvas fallback's centre dot (the glow dot ring is shape renderer only)
        const float dot = SOL::HudGeometry::RETICLE_CANVAS_DOT_PX;
        DrawRect(HUD_PRIMARY, center.X - 0.5f * dot, center.Y - 0.5f * dot, dot, dot);
    }
}

//////////////////////////////////////////////////////////////////////////
// Draws the virtual-joystick circle, dead zone and the current stick offset (yellow during free look), and the steer
// hint until the player first steers this session
void ASOLFlightHud::DrawJoystick(const FVector2D& center, const FSOLHudPilotSnapshot& pilot)
{
    // Yellow while Alt free-look is held (the mouse orbits the camera, not the stick; issue #31): the builder picks the
    // role, and the circle (its first primitive) is the colour recorded for verification
    mSymbolScratch.Reset();
    SOLHudSymbols::AppendJoystick(center, static_cast<float>(pilot.StickRadiusPx), pilot.StickOffsetPx,
        pilot.bIsFreeLooking, pilot.bIsSteering, mUiScale, mSymbolScratch);
    mLastJoystickColor = mSymbolScratch.Count > 0 ? HudRoleColor(mSymbolScratch.Prims[0].Role)
        : FLinearColor::Transparent;
    ReplaySymbolPrims(mSymbolScratch);

    // Until the player first steers this session, a hint names the steer-hold key (issue #31)
    if (pilot.bShowSteerHint)
    {
        mText.Reset();
        mText.Append(mKeyTexts.SteerHint);
        AddHoloPanel(DrawBufferCentered(ESOLHudLine::SteerHint, HudDimText(HUD_PRIMARY), Canvas->ClipX * 0.5f,
            Canvas->ClipY * HUD_STEER_HINT_Y_FRACTION, ESOLHudFont::Medium), HUD_PRIMARY);
        mWasSteerHintDrawn = true;
    }
}

//////////////////////////////////////////////////////////////////////////
// Draws the sim date and warp, anchor, nearest body and altitude in a panel, and the key hints (top left)
void ASOLFlightHud::DrawInfoBlock()
{
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    const float x = HUD_MARGIN_PX * mUiScale;
    float y = HUD_MARGIN_PX * mUiScale;
    FBox2D panel(ForceInit);

    // The panel's lines are the date-and-anchor card's content (SDD 42 D15): on the menu layer, above its backing
    const bool bIsCardContent = BeginCardContent(ESOLHudCard::DateAnchor);

    // Sim date and time (UTC) and the warp label
    const FDateTime utc = SimClock->GetUtcDateTime();
    const double warp = SimClock->GetClock().GetWarpFactor();
    mText.Reset();
    mText.Appendf(TEXT("%04d-%02d-%02d  %02d:%02d:%02d UTC    WARP "), utc.GetYear(), utc.GetMonth(), utc.GetDay(),
        utc.GetHour(), utc.GetMinute(), utc.GetSecond());
    if (warp >= SOL::SECONDS_PER_DAY)
    {
        mText.Appendf(TEXT("%.0f d/s"), warp / SOL::SECONDS_PER_DAY);
    }
    else if (warp >= SOL::SECONDS_PER_HOUR)
    {
        mText.Appendf(TEXT("%.0f h/s"), warp / SOL::SECONDS_PER_HOUR);
    }
    else
    {
        mText.Appendf(TEXT("%.0fx"), warp);
    }
    panel += DrawBuffer(ESOLHudLine::DateWarp, warp > 1.0 ? HUD_SECONDARY : HUD_PRIMARY, x, y, ESOLHudFont::Medium);
    y += HUD_LINE_MEDIUM_PX * mUiScale;

    // Anchor body
    const int32 anchorIndex = AnchorSubsystem->GetAnchorIndex();
    mText.Reset();
    mText.Append(TEXT("ANCHOR  "));
    if (anchorIndex != INDEX_NONE)
    {
        registry.GetName(anchorIndex).AppendString(mText);
    }
    panel += DrawBuffer(ESOLHudLine::Anchor, HUD_PRIMARY, x, y, ESOLHudFont::Medium);
    y += HUD_LINE_MEDIUM_PX * mUiScale;

    // Nearest body and the altitude above its surface
    double altitudeM = 0.0;
    const int32 nearest = AnchorSubsystem->FindNearestBody(altitudeM);
    mText.Reset();
    mText.Append(TEXT("NEAREST  "));
    if (nearest != INDEX_NONE)
    {
        registry.GetName(nearest).AppendString(mText);
        mText.Append(TEXT("   ALT "));
        SOLHudFormat::AppendDistanceM(mText, altitudeM);
    }
    panel += DrawBuffer(ESOLHudLine::Nearest, HUD_PRIMARY, x, y, ESOLHudFont::Medium);
    y += HUD_LINE_MEDIUM_PX * mUiScale;

    // Surface-lock (SDD 4 decision 6): the status while engaged (warning color past the mode's warn threshold), else a
    // hint while the nearest body is within its manual range (not while the jump map is open, where L does nothing); no
    // line at all when nothing is in reach. DrawHUD has already reset mLastSurfaceLockLine for this frame
    if (Ships != nullptr && Ships->HasPlayerShip())
    {
        const FSOLSurfaceLockState& lock = Ships->GetSurfaceLockState();
        mText.Reset();
        if (lock.bEngaged && lock.BodyIndex >= 0 && lock.BodyIndex < registry.Num())
        {
            mText.Append(TEXT("SURFACE LOCK  "));
            registry.GetName(lock.BodyIndex).AppendString(mText);
            mText.Append(TEXT("   ALT "));
            SOLHudFormat::AppendDistanceM(mText, Ships->GetSurfaceLockAltitudeM());
            mLastSurfaceLockLine = lock.bWarning ? ESOLSurfaceLockHudLine::Warning : ESOLSurfaceLockHudLine::Engaged;
            panel += DrawBuffer(ESOLHudLine::SurfaceLock, lock.bWarning ? HUD_SECONDARY : HUD_PRIMARY, x, y,
                ESOLHudFont::Medium);
            y += HUD_LINE_MEDIUM_PX * mUiScale;
        }
        else if (!IsMapOpen() && nearest != INDEX_NONE && altitudeM <= SOLSurfaceLock::ComputeManualRangeM(
            registry.GetRadiusM(nearest), Ships->GetSurfaceLockParams()))
        {
            mText.Append(mKeyTexts.SurfaceLockHint);
            registry.GetName(nearest).AppendString(mText);
            mText.Append(TEXT("   ALT "));
            SOLHudFormat::AppendDistanceM(mText, altitudeM);
            mLastSurfaceLockLine = ESOLSurfaceLockHudLine::Hint;
            panel += DrawBuffer(ESOLHudLine::SurfaceLock, HudDimText(HUD_PRIMARY), x, y, ESOLHudFont::Medium);
            y += HUD_LINE_MEDIUM_PX * mUiScale;
        }
    }
    if (bIsCardContent)
    {
        EndCardContent();
    }
    DrawCard(ESOLHudCard::DateAnchor, panel, mAlwaysOnCardLook);

    // Overlay state and key hints (the jump map's own keys while it is open), below the panel
    y += HUD_PANEL_GAP_PX * mUiScale;
    mText.Reset();
    if (IsMapOpen())
    {
        mText.Append(mMapHints);
        mLastHintsRect += DrawBuffer(ESOLHudLine::Hints, HUD_PRIMARY, x, y, ESOLHudFont::Small);
        y += HUD_LINE_SMALL_PX * mUiScale;

        // Destination picking (2d) and the jump (2e)
        mText.Reset();
        mText.Append(TEXT("PICK   left click body/space + drag: XY   hold Shift + move: height   Shift+click: lock"));
        const bool bRouteMode = MapMode != nullptr && MapMode->GetMapMode() != ESOLMapMode::Jump;
        mText.Append(bRouteMode ? TEXT("   X clear   Enter start route") : TEXT("   X clear   Enter jump"));
        mLastHintsRect += DrawBuffer(ESOLHudLine::MapPickHints, HUD_PRIMARY, x, y, ESOLHudFont::Small);
        return;
    }
    mText.Append(AreOrbitLinesVisible() ? mFlightHintsOrbitsOn : mFlightHintsOrbitsOff);
    mLastHintsRect += DrawBuffer(ESOLHudLine::Hints, HudDimText(HUD_PRIMARY), x, y, ESOLHudFont::Small);
}

//////////////////////////////////////////////////////////////////////////
// Draws the relative and absolute speed, the cap and its fill bar, assist and boost (bottom left)
void ASOLFlightHud::DrawSpeedBlock(const FSOLShipState& state, const FSOLShipControl& control)
{
    const float x = HUD_MARGIN_PX * mUiScale;
    const float blockHeight = (HUD_LINE_LARGE_PX + 2.0f * HUD_LINE_SMALL_PX + HUD_LINE_MEDIUM_PX
        + SOL::HudGeometry::SPEED_BAR_HEIGHT_PX + 6.0f + HUD_LINE_SMALL_PX) * mUiScale;
    float y = Canvas->ClipY - HUD_MARGIN_PX * mUiScale - blockHeight;
    const double relativeSpeedMps = (state.VelocityMps - Ships->GetReferenceVelocityMps()).Size();

    // The block is the speed card's content (SDD 42 D15): its text, bar and icon on the menu layer, above its backing
    const bool bIsCardContent = BeginCardContent(ESOLHudCard::Speed);

    // Speed relative to the active frame, large
    mText.Reset();
    AppendSpeed(relativeSpeedMps);
    mLeftBlockRect += DrawBuffer(ESOLHudLine::Speed, HUD_PRIMARY, x, y, ESOLHudFont::Large, HUD_SPEED_TEXT_SCALE);
    y += HUD_LINE_LARGE_PX * mUiScale;

    // Frame name (locked target, else anchor) and the absolute Sun-frame speed, smaller
    mText.Reset();
    mText.Append(TEXT("rel "));
    Targeting->GetFrameName().AppendString(mText);
    if (Targeting->IsFrameLocked())
    {
        mText.Append(mKeyTexts.FrameLockTag);
    }
    mLeftBlockRect += DrawBuffer(ESOLHudLine::Frame, Targeting->IsFrameLocked() ? HUD_SECONDARY : HUD_PRIMARY, x, y,
        ESOLHudFont::Small);
    y += HUD_LINE_SMALL_PX * mUiScale;
    mText.Reset();
    mText.Append(TEXT("abs "));
    AppendSpeed(state.VelocityMps.Size());
    mText.Append(TEXT(" (Sun frame)"));
    mLeftBlockRect += DrawBuffer(ESOLHudLine::AbsoluteSpeed, HudDimText(HUD_PRIMARY), x, y, ESOLHudFont::Small);
    y += HUD_LINE_SMALL_PX * mUiScale;

    // Speed cap
    mText.Reset();
    mText.Append(TEXT("CAP "));
    AppendSpeed(control.SpeedCapMps);
    mLeftBlockRect += DrawBuffer(ESOLHudLine::Cap, HUD_PRIMARY, x, y, ESOLHudFont::Medium);
    y += HUD_LINE_MEDIUM_PX * mUiScale;

    // Fill bar of the relative speed against the cap (Secondary above the cap, e.g. while boosting)
    const double fill = control.SpeedCapMps > 0.0 ? relativeSpeedMps / control.SpeedCapMps : 0.0;
    DrawBar(x, y, fill, fill > 1.0 ? ESOLHudPaletteRole::Secondary : ESOLHudPaletteRole::Primary);
    mLeftBlockRect += FBox2D(FVector2D(x, y), FVector2D(x + SOL::HudGeometry::SPEED_BAR_WIDTH_PX * mUiScale,
        y + SOL::HudGeometry::SPEED_BAR_HEIGHT_PX * mUiScale));
    y += (SOL::HudGeometry::SPEED_BAR_HEIGHT_PX + 6.0f) * mUiScale;

    // Flight mode label in its color (SDD 19 section 3.7) and boost, then the mouse-steer icon to their right
    const ESOLFlightMode mode = Ships->GetFlightMode();
    mText.Reset();
    mText.Append(SOLHudFormat::FlightModeLabel(mode));
    mText.Append(control.bBoost ? TEXT("   BOOST") : TEXT("   boost off"));
    const FBox2D modeRect = DrawBuffer(ESOLHudLine::Assist, SOLHudFormat::FlightModeColor(mode), x, y,
        ESOLHudFont::Small);
    mLeftBlockRect += modeRect;
    if (HasFreshPilot())
    {
        const FSOLHudPilotSnapshot& pilot = HudEvents->GetBus().GetPilot();
        const float iconRadius = SOL::HudGeometry::STEER_ICON_RADIUS_PX * mUiScale;
        const FVector2D iconCenter(modeRect.Max.X + (HUD_STEER_ICON_GAP_PX + SOL::HudGeometry::STEER_ICON_RADIUS_PX)
            * mUiScale, 0.5 * (modeRect.Min.Y + modeRect.Max.Y));
        DrawSteerIcon(iconCenter, pilot.SteerIcon, pilot.bIsSteering);
        mLeftBlockRect += FBox2D(iconCenter - FVector2D(iconRadius), iconCenter + FVector2D(iconRadius));
    }
    if (bIsCardContent)
    {
        EndCardContent();
    }
}

//////////////////////////////////////////////////////////////////////////
// Draws the mouse-steer icon: an open cursor arrow (Free), a reticle (Held) or a reticle with a lock tick (Locked)
void ASOLFlightHud::DrawSteerIcon(const FVector2D& center, const ESOLSteerIcon icon, const bool bSteering)
{
    mSymbolScratch.Reset();
    SOLHudSymbols::AppendSteerIcon(center, icon, bSteering, mUiScale, mSymbolScratch);
    ReplaySymbolPrims(mSymbolScratch);
}

//////////////////////////////////////////////////////////////////////////
// Draws the pilot G meter (above the speed block), the G warning (under the reticle) and the autopilot marker
// with its ETA and the high-warp hint (top centre)
void ASOLFlightHud::DrawPilotBlock(const FSOLShipControl& control)
{
    const FSOLPilotGLimits limits = Ships->GetFlightParams().PilotLimits;
    const FSOLGMeterState meter = Ships->GetGMeter();
    const double gLoad = Ships->GetGLoadG();
    const ESOLGWarning warning = SOLNewtonian::GWarningLevel(meter, gLoad, limits);
    const bool bAlarm = warning == ESOLGWarning::Critical || warning == ESOLGWarning::Blackout
        || warning == ESOLGWarning::Lethal;

    // G meter: the G-load and a bar of the tolerance left, above the speed block (bottom left)
    const float x = HUD_MARGIN_PX * mUiScale;
    const float speedBlockHeight = (HUD_LINE_LARGE_PX + 2.0f * HUD_LINE_SMALL_PX + HUD_LINE_MEDIUM_PX
        + SOL::HudGeometry::SPEED_BAR_HEIGHT_PX + 6.0f + HUD_LINE_SMALL_PX) * mUiScale;
    const float barHeight = SOL::HudGeometry::SPEED_BAR_HEIGHT_PX * mUiScale;
    float y = Canvas->ClipY - HUD_MARGIN_PX * mUiScale - speedBlockHeight - HUD_G_BLOCK_GAP_PX * mUiScale
        - barHeight - HUD_LINE_SMALL_PX * mUiScale;
    const ESOLHudPaletteRole meterRole = bAlarm ? ESOLHudPaletteRole::Bad
        : (warning == ESOLGWarning::OverSustained ? ESOLHudPaletteRole::Secondary : ESOLHudPaletteRole::Primary);
    const FLinearColor& meterColor = HudRoleColor(meterRole);

    // The G meter is part of the speed card's content (SDD 42 D15), on the menu layer; the warning stays HUD
    const bool bIsCardContent = BeginCardContent(ESOLHudCard::Speed);
    mText.Reset();
    mText.Appendf(TEXT("G %.1f   tolerance %.0f%%"), gLoad, meter.Meter * HUD_PERCENT);
    mLeftBlockRect += DrawBuffer(ESOLHudLine::GMeter, meterColor, x, y, ESOLHudFont::Small);
    y += HUD_LINE_SMALL_PX * mUiScale;
    DrawBar(x, y, meter.Meter, meterRole);
    const FSOLChaseStatus chase = Ships->GetChaseStatus();
    if (chase.bActive)
    {
        // Chase Down: a floor tick at the safety margin and a fainter resume tick while cooling toward a reachable
        // resume meter (SDD 32 section 3.9)
        const bool bShowResume = chase.bCooling && SOLHudFormat::IsChaseResumeShown(chase.ResumeMeter);
        mSymbolScratch.Reset();
        SOLHudSymbols::AppendChaseTicks(x, y, chase.FloorMeter, bShowResume, chase.ResumeMeter, mUiScale,
            mSymbolScratch);
        ReplaySymbolPrims(mSymbolScratch);
    }
    mLeftBlockRect += FBox2D(FVector2D(x, y), FVector2D(x + SOL::HudGeometry::SPEED_BAR_WIDTH_PX * mUiScale,
        y + barHeight));
    if (bIsCardContent)
    {
        EndCardContent();
    }

    // Warning under the reticle in its own panel: over the sustained limit only in manual flight (assist never
    // exceeds it), the critical meter and the blackout in every mode
    const bool bShowWarning = bAlarm || (warning == ESOLGWarning::OverSustained && !control.bFlightAssist);
    if (bShowWarning)
    {
        mText.Reset();
        switch (warning)
        {
        case ESOLGWarning::Lethal:
            mText.Appendf(TEXT("LETHAL G - %.1f G FOR %.0f S"), gLoad, meter.DepletedS);
            break;
        case ESOLGWarning::Blackout:
            mText.Append(TEXT("BLACKOUT - NO CONTROL"));
            break;
        case ESOLGWarning::Critical:
            mText.Appendf(TEXT("G CRITICAL - EASE OFF (%.1f G)"), gLoad);
            break;
        default:
            mText.Appendf(TEXT("G LIMIT EXCEEDED: %.1f G (sustained %.1f G)"), gLoad, limits.SustainedG);
            break;
        }
        const FLinearColor& warningColor = bAlarm ? HUD_BAD : HUD_SECONDARY;
        AddHoloPanel(DrawBufferCentered(ESOLHudLine::GWarning, warningColor, Canvas->ClipX * 0.5f,
            Canvas->ClipY * HUD_G_WARNING_Y_FRACTION, ESOLHudFont::Medium), warningColor);
    }

    // Top marker: the chase (or its target-lost note) in place of the autopilot marker, else the autopilot marker
    // with the ETA countdown (sim time) and the high-warp precision hint
    const bool bChase = chase.bActive;
    const bool bLostNote = !bChase && Interaction != nullptr && Interaction->IsChaseTargetLostNoteShown();
    const int32 autopilotBody = (bChase || bLostNote) ? INDEX_NONE : Ships->GetAutopilotBodyIndex();
    if (!bChase && !bLostNote && autopilotBody == INDEX_NONE)
    {
        return;
    }
    mText.Reset();
    if (bChase)
    {
        // The name through a stack builder: no heap string per frame
        const FNameBuilder targetName(chase.TargetName);
        SOLHudFormat::AppendChaseMarker(mText, targetName.ToView(), chase.TargetDistanceM, chase.ClosingSpeedMps,
            chase.TimeToInterceptS, chase.bHolding);
    }
    else if (bLostNote)
    {
        mText.Append(SOLMenuText::CHASE_ENDED_NOTE);
    }
    else
    {
        const FSOLGuidanceOutput guidance = Ships->GetAutopilotGuidance();
        mText.Append(TEXT("AUTOPILOT > "));
        BodyRegistry->GetRegistry().GetName(autopilotBody).AppendString(mText);
        mText.Append(TEXT("   ETA "));
        SOLHudFormat::AppendDurationS(mText, guidance.EtaS);
        mText.Append(TEXT("   "));
        SOLHudFormat::AppendDistanceM(mText, guidance.DistanceM);
    }
    const float textWidth = static_cast<float>(MeasureLine(ESOLHudLine::Autopilot, ESOLHudFont::Medium).X);

    // The block sits below the top edge, or below the key-hint lines where it would overlap them (long hint lines at
    // 1280x720)
    const float centerX = Canvas->ClipX * 0.5f;
    float top = HUD_AUTOPILOT_TOP_PX * mUiScale;
    const float halfWidth = 0.5f * textWidth * mUiScale;
    const float blockHeight = (HUD_LINE_MEDIUM_PX + 2.0f * HUD_LINE_SMALL_PX) * mUiScale;
    if (mLastHintsRect.bIsValid && mLastHintsRect.Min.X < centerX + halfWidth
        && mLastHintsRect.Max.X > centerX - halfWidth && mLastHintsRect.Min.Y < top + blockHeight
        && mLastHintsRect.Max.Y > top)
    {
        top = static_cast<float>(mLastHintsRect.Max.Y) + HUD_LINE_SMALL_PX * mUiScale;
    }
    mTopBlockRect += DrawBuffer(ESOLHudLine::Autopilot, bLostNote ? HUD_BAD : HUD_SECONDARY, centerX - halfWidth, top,
        ESOLHudFont::Medium);
    if (bLostNote)
    {
        mAutopilotBlockBottomY = top + HUD_LINE_MEDIUM_PX * mUiScale;
        return;
    }
    float lineY = top + HUD_LINE_MEDIUM_PX * mUiScale;
    if (bChase)
    {
        // Second line: the activity and its G limit
        mText.Reset();
        SOLHudFormat::AppendChaseActivity(mText, chase.Activity, chase.LimitG, chase.ResumeMeter);
        mTopBlockRect += DrawBufferCentered(ESOLHudLine::ChaseActivity, HUD_SECONDARY, centerX, lineY,
            ESOLHudFont::Small);
        lineY += HUD_LINE_SMALL_PX * mUiScale;
    }
    mText.Reset();
    mText.Append(bChase ? SOLMenuText::CHASE_CANCEL_HINT : SOLMenuText::AUTOPILOT_CANCEL_HINT);
    mTopBlockRect += DrawBufferCentered(ESOLHudLine::AutopilotCancel, HudDimText(HUD_PRIMARY), centerX, lineY,
        ESOLHudFont::Small);
    lineY += HUD_LINE_SMALL_PX * mUiScale;
    if (!bChase && Ships->IsAutopilotPrecisionReduced())
    {
        mText.Reset();
        mText.Append(TEXT("WARP: autopilot precision reduced"));
        mTopBlockRect += DrawBufferCentered(ESOLHudLine::AutopilotHint, HUD_SECONDARY, centerX, lineY,
            ESOLHudFont::Small);
        lineY += HUD_LINE_SMALL_PX * mUiScale;
    }
    mAutopilotBlockBottomY = lineY;
}

//////////////////////////////////////////////////////////////////////////
// Fills the hologram overlay's inputs from the map camera and the routes and draws the grid and stems (map only)
void ASOLFlightHud::DrawHologramOverlay(const FSOLMapOverlayView& overlayView)
{
    FSOLHologramOverlayInputs inputs;
    const FSOLOrbitCameraState& camera = MapMode->GetCameraState();
    inputs.FocusM = camera.FocusPositionM;
    inputs.CameraDistanceM = camera.DistanceM;
    if (Routes != nullptr && Routes->GetState() != ESOLRouteState::None)
    {
        inputs.RouteWaypoints = Routes->GetWaypoints();
    }
    if (MapMode->GetMapMode() != ESOLMapMode::Jump)
    {
        inputs.DraftWaypoints = MapMode->GetModeState().Draft;
    }
    mHologramOverlay.Draw(*Canvas, overlayView, *AnchorSubsystem, BodyRegistry->GetRegistry(), inputs, mUiScale);
}

//////////////////////////////////////////////////////////////////////////
// Fills the route overlay's inputs from the route and map-mode subsystems and draws it (map, or flight with P on)
void ASOLFlightHud::DrawRouteOverlay(const FSOLMapOverlayView& overlayView)
{
    FSOLRouteOverlayInputs inputs;
    inputs.GlowScale = mHudGlowScale;
    const ESOLRouteState state = Routes->GetState();
    if (state != ESOLRouteState::None)
    {
        inputs.RouteWaypoints = Routes->GetWaypoints();
        inputs.RouteCursor = Routes->GetCursor();
        inputs.RouteKind = Routes->GetKind();
        inputs.RouteStyle = state == ESOLRouteState::Paused ? ESOLRouteDrawStyle::Paused
            : (state == ESOLRouteState::Finished ? ESOLRouteDrawStyle::Finished : ESOLRouteDrawStyle::Active);
    }
    if (Ships != nullptr && Ships->HasPlayerShip())
    {
        inputs.bHasShip = true;
        inputs.ShipPositionM = Ships->GetUniversePositionM();
    }
    inputs.PlanePointM = inputs.bHasShip ? inputs.ShipPositionM : AnchorSubsystem->GetViewpointM();

    // The draft (hidden in the map's Jump mode) and, while a pick drags in Move / Patrol mode, the cursor point
    const bool bMapOpen = IsMapOpen();
    if (MapMode != nullptr)
    {
        const FSOLMapModeState& modeState = MapMode->GetModeState();
        const bool bRouteMode = modeState.Mode != ESOLMapMode::Jump;
        if (!bMapOpen || bRouteMode)
        {
            inputs.DraftWaypoints = modeState.Draft;
            inputs.DraftKind = SOLMapModeState::RouteKindFor(modeState);
        }
        if (bMapOpen && bRouteMode && MapMode->IsPickDragging() && MapMode->GetPickState().bHasReference)
        {
            const FVector2D planarM = MapMode->GetPickPlanarOffsetM();
            inputs.CursorPointM = SOLMapPicking::ComposeDestination(MapMode->GetPickReferencePositionM(), planarM.X,
                planarM.Y, 0.0, SOL::MAP_PICK_ECLIPTIC_X, SOL::MAP_PICK_ECLIPTIC_Y, SOL::MAP_PICK_UP);
            inputs.bHasCursorPoint = true;
        }
        if (bMapOpen)
        {
            inputs.PlanePointM = MapMode->GetCameraState().FocusPositionM;
        }
    }
    mRouteOverlay.Draw(*Canvas, overlayView, *AnchorSubsystem, BodyRegistry->GetRegistry(), inputs,
        GEngine->GetSmallFont(), mUiScale);
}

//////////////////////////////////////////////////////////////////////////
// Draws the route marker (state, progress, total ETA) below the autopilot marker, moved below the key hints if it
// would overlap them (top centre)
void ASOLFlightHud::DrawRouteMarker()
{
    if (Routes == nullptr || Routes->GetState() == ESOLRouteState::None)
    {
        return;
    }
    const ESOLRouteState state = Routes->GetState();
    const ESOLRouteKind kind = Routes->GetKind();
    const FSOLRouteCursor& cursor = Routes->GetCursor();
    const int32 waypointCount = Routes->GetWaypoints().Num();

    // "ROUTE  MOVE  leg 2/5  40%   ETA 1:02:03", "ROUTE  PATROL LOOP  waypoint 3/4  legs 7   next ETA 5:00"
    mText.Reset();
    mText.Append(TEXT("ROUTE  "));
    mText.Append(kind == ESOLRouteKind::Move ? TEXT("MOVE") : (kind == ESOLRouteKind::PatrolLoop
        ? TEXT("PATROL LOOP") : TEXT("PATROL PING-PONG")));
    if (kind == ESOLRouteKind::Move)
    {
        const int32 totalLegs = SOLRoute::TotalLegs(kind, waypointCount);
        const int32 legNumber = state == ESOLRouteState::Finished ? totalLegs
            : FMath::Min(cursor.LegsCompleted + 1, totalLegs);
        mText.Appendf(TEXT("  leg %d/%d  %.0f%%"), legNumber, totalLegs, Routes->GetProgress() * HUD_PERCENT);
    }
    else
    {
        mText.Appendf(TEXT("  waypoint %d/%d  legs %d"), cursor.TargetIndex + 1, waypointCount, cursor.LegsCompleted);
    }
    FLinearColor color = HUD_PRIMARY;
    switch (state)
    {
    case ESOLRouteState::Paused:
        mText.Append(TEXT("   PAUSED"));
        mText.Append(mKeyTexts.RouteResumeHint);
        color = HUD_SECONDARY;
        break;
    case ESOLRouteState::Finished:
        mText.Append(TEXT("   COMPLETE"));
        color = HudDimText(HUD_PRIMARY);
        break;
    default:
        mText.Append(kind == ESOLRouteKind::Move ? TEXT("   ETA ") : TEXT("   next ETA "));
        SOLHudFormat::AppendDurationS(mText, Routes->GetRouteEtaS());
        break;
    }

    // Below the autopilot block (or where it would be), and below the key hints if it would overlap them
    const FVector2D textSize = MeasureLine(ESOLHudLine::Route, ESOLHudFont::Medium);
    const float textWidth = static_cast<float>(textSize.X);
    const float textHeight = static_cast<float>(textSize.Y);
    const float x = Canvas->ClipX * 0.5f - textWidth * 0.5f * mUiScale;
    float y = mAutopilotBlockBottomY > 0.0f ? mAutopilotBlockBottomY : HUD_AUTOPILOT_TOP_PX * mUiScale;
    FBox2D rect(FVector2D(x, y), FVector2D(x + textWidth * mUiScale, y + textHeight * mUiScale));
    if (mLastHintsRect.bIsValid && rect.Intersect(mLastHintsRect))
    {
        y = mLastHintsRect.Max.Y + HUD_ROUTE_HINT_GAP_PX * mUiScale;
        rect = FBox2D(FVector2D(x, y), FVector2D(x + textWidth * mUiScale, y + textHeight * mUiScale));
    }
    DrawBuffer(ESOLHudLine::Route, color, x, y, ESOLHudFont::Medium);
    mTopBlockRect += rect;
    mLastRouteLineRect = rect;
    mLastRouteLineText.Append(mText);
}

//////////////////////////////////////////////////////////////////////////
// Draws the selected target's readout panel and bracket, or an edge arrow when it is off-screen
void ASOLFlightHud::DrawTarget(const FSOLHudView& view, const FSOLShipState& state)
{
    const FSOLTargetInfo* target = Targeting->GetSelectedTarget();
    const int32 selectedIndex = Targeting->GetSelectedIndex();

    // The target card (SDD 42 D15): the live readout is cached, so the card closes from it once the target is cleared;
    // no cue (a target change is frequent, SDD 42 section 5). Another target (R / F cycling) keeps the card open and
    // only swaps its text (Q15): it draws in only when a target appears from none, and closes only when cleared
    const bool bLive = target != nullptr;
    const double distanceM = bLive ? FVector3d::Dist(target->PositionM, state.PositionM) : 0.0;
    if (bLive)
    {
        mTargetCardName = target->Name;
        mTargetCardDistanceM = distanceM;
        mTargetCardRelativeMps = (state.VelocityMps - target->VelocityMps).Size();
    }
    const bool bContinuous = mTargetCardFrame != 0 && GFrameCounter <= mTargetCardFrame + 1;
    mTargetCardFrame = GFrameCounter;
    HudCardUpdate(mTargetCardAnim, bLive, false, bContinuous, mCardNowS, mCardStrength);
    const FSOLHoloMenuLook look = CardLook(mTargetCardAnim, bLive);
    if (look.bVisible)
    {
        DrawTargetCard(look);
    }
    if (!bLive)
    {
        // "No target" stays a plain HUD line with its HUD panel, shown once the card has closed
        if (!look.bVisible)
        {
            mText.Reset();
            mText.Append(TEXT("TARGET  "));
            mText.Append(mKeyTexts.NoTarget);
            AddHoloPanel(DrawBuffer(ESOLHudLine::TargetName, HudDimText(HUD_PRIMARY),
                Canvas->ClipX - (HUD_MARGIN_PX + HUD_TARGET_PANEL_WIDTH_PX) * mUiScale, HUD_MARGIN_PX * mUiScale,
                ESOLHudFont::Medium), HUD_PRIMARY);
        }
        return;
    }

    // Where the target is drawn: a body through the same placement as its visual, anything else through the point
    // placement from the current render viewpoint (correct while the map overrides the viewpoint)
    FVector renderCm;
    double radiusCm = 0.0;
    if (selectedIndex >= 0 && selectedIndex < BodyRegistry->GetRegistry().Num())
    {
        const FSOLRenderPlacement placement = AnchorSubsystem->ComputeBodyRenderPlacement(selectedIndex);
        renderCm = placement.LocationCm;
        radiusCm = placement.RadiusCm;
    }
    else
    {
        // Candidates are Unreal-handed; the placement takes ecliptic universe metres (the Y flip is its own inverse)
        renderCm = AnchorSubsystem->ComputePointRenderLocationCm(SOLRender::EclipticToUnreal(target->PositionM));
        radiusCm = target->RadiusM * SOL::METERS_TO_CM;
    }
    const FVector local = view.Rotation.UnrotateVector(renderCm - view.Location);
    FVector2D screen;
    if (ProjectLocal(view, local, screen) && IsOnScreen(screen, HUD_EDGE_ARROW_MARGIN_PX * mUiScale))
    {
        // Corner bracket sized to the target's apparent radius, with its name and distance beneath
        const float half = FMath::Clamp(static_cast<float>(view.FocalPx * radiusCm / local.X),
            SOL::HudGeometry::BRACKET_MIN_HALF_PX * mUiScale, SOL::HudGeometry::BRACKET_MAX_HALF_PX * mUiScale);
        mSymbolScratch.Reset();
        SOLHudSymbols::AppendTargetBracket(screen, half, mSymbolScratch);
        ReplaySymbolPrims(mSymbolScratch);
        mText.Reset();
        target->Name.AppendString(mText);
        mText.Append(TEXT("  "));
        SOLHudFormat::AppendDistanceM(mText, distanceM);
        AddHoloPanel(DrawBuffer(ESOLHudLine::TargetLabel, HUD_SECONDARY, screen.X - half,
            screen.Y + half + 4.0f * mUiScale, ESOLHudFont::Small), HUD_SECONDARY);
        return;
    }

    // Off-screen or behind: an arrow on the screen edge pointing toward it (camera-local right/up give the direction)
    FVector2D direction(local.Y, -local.Z);
    if (!direction.Normalize())
    {
        direction = FVector2D(0.0, 1.0);
    }
    const float margin = HUD_EDGE_ARROW_MARGIN_PX * mUiScale;
    const double halfWidth = view.Center.X - margin;
    const double halfHeight = view.Center.Y - margin;
    const double reach = FMath::Min(FMath::Abs(direction.X) > UE_KINDA_SMALL_NUMBER
        ? halfWidth / FMath::Abs(direction.X) : TNumericLimits<double>::Max(),
        FMath::Abs(direction.Y) > UE_KINDA_SMALL_NUMBER ? halfHeight / FMath::Abs(direction.Y)
        : TNumericLimits<double>::Max());
    const FVector2D anchor = view.Center + direction * reach;
    mSymbolScratch.Reset();
    SOLHudSymbols::AppendTargetEdgeArrow(anchor, direction, mUiScale, mSymbolScratch);
    ReplaySymbolPrims(mSymbolScratch);
    mText.Reset();
    target->Name.AppendString(mText);
    const FVector2D label = anchor - direction * (3.0f * SOL::HudGeometry::EDGE_ARROW_LENGTH_PX * mUiScale);
    DrawBuffer(ESOLHudLine::TargetLabel, HUD_SECONDARY, label.X - 20.0f * mUiScale, label.Y - 8.0f * mUiScale,
        ESOLHudFont::Small);
}

//////////////////////////////////////////////////////////////////////////
// Draws the target card (top right) from the cached readout at its look: name, distance and relative speed, faded by
// the fill, on the menu layer
void ASOLFlightHud::DrawTargetCard(const FSOLHoloMenuLook& look)
{
    const float panelX = Canvas->ClipX - (HUD_MARGIN_PX + HUD_TARGET_PANEL_WIDTH_PX) * mUiScale;
    float panelY = HUD_MARGIN_PX * mUiScale;
    const FLinearColor color = SOLHoloRadial::Faded(HUD_SECONDARY, look.FillAlpha);
    BeginReadoutGlow(ESOLHudCard::Target);
    mText.Reset();
    mText.Append(TEXT("TARGET  "));
    mTargetCardName.AppendString(mText);
    FBox2D readout = MenuText(ESOLHudLine::TargetName, color, panelX, panelY, ESOLHudFont::Medium);
    panelY += HUD_LINE_MEDIUM_PX * mUiScale;
    mText.Reset();
    mText.Append(TEXT("DIST "));
    SOLHudFormat::AppendDistanceM(mText, mTargetCardDistanceM);
    readout += MenuText(ESOLHudLine::TargetDistance, color, panelX, panelY, ESOLHudFont::Small);
    panelY += HUD_LINE_SMALL_PX * mUiScale;
    mText.Reset();
    mText.Append(TEXT("REL "));
    AppendSpeed(mTargetCardRelativeMps);
    readout += MenuText(ESOLHudLine::TargetRelative, color, panelX, panelY, ESOLHudFont::Small);
    EndReadoutGlow();
    DrawCard(ESOLHudCard::Target, readout, look);
}

//////////////////////////////////////////////////////////////////////////
// Draws the overlay icons and edge arrows (only while the icons overlay shows, flight view only) and chooses the NPC
// ships for the icons and the radar
void ASOLFlightHud::DrawOverlayIcons(const FSOLHudView* view)
{
    using SOLOverlay::EOverlay;
    using SOLOverlayIcons::EIconCategory;
    mAreShipsSelected = false;
    mIconLayer.ClearStats();

    // Returns whether an overlay row shows now (every row with its default and the master on without settings)
    const SOLOverlay::FOverlayPrefs defaultPrefs = SOLOverlay::MakeDefaultPrefs();
    const auto isShown = [this, &defaultPrefs](const EOverlay overlay)
    {
        return Settings != nullptr ? Settings->IsOverlayShown(overlay) : SOLOverlay::IsShown(defaultPrefs, overlay);
    };
    FSOLOverlayIconFrame frame;
    for (int32 category = 0; category < static_cast<int32>(EIconCategory::Count); ++category)
    {
        frame.bCategoryShown[category] = isShown(EOverlay::Icons)
            && isShown(SOLOverlayIcons::CategoryOverlay(static_cast<EIconCategory>(category)));
    }
    frame.bDarkening = isShown(EOverlay::Darkening);
    frame.ViewpointM = AnchorSubsystem->GetViewpointM();
    if (Npcs != nullptr)
    {
        frame.Ships = Npcs->GetCpuShips();
    }

    // The ships are chosen when the icons or the radar need them
    if (frame.bCategoryShown[static_cast<int32>(EIconCategory::Ship)] || isShown(EOverlay::ShipsOnRadar))
    {
        mIconLayer.SelectShips(frame);
        mAreShipsSelected = true;
    }
    if (view == nullptr || mBatch == nullptr || !isShown(EOverlay::Icons))
    {
        return;
    }
    frame.LocationCm = view->Location;
    frame.Rotation = view->Rotation;
    frame.CenterPx = view->Center;
    frame.SizePx = FVector2D(Canvas->ClipX, Canvas->ClipY);
    frame.FocalPx = view->FocalPx;
    frame.UiScale = mUiScale;
    frame.Centuries = SimClock->GetClock().GetCenturiesSinceJ2000();
    if (Combat != nullptr)
    {
        frame.TargetActive = Combat->GetTargetActive();
        frame.TargetPositionsM = Combat->GetTargetPositionsM();
        frame.TargetRadiusM = Combat->GetTargetRadiusM();
    }
    if (Interaction != nullptr)
    {
        frame.Highlighted = Interaction->GetHovered();
        frame.Selected = Interaction->GetSelectedObject();
    }
    mIconLayer.Draw(*mBatch, frame, BodyRegistry->GetRegistry(), *AnchorSubsystem);
    if (Interaction != nullptr)
    {
        // The icons as drawn are what the free cursor hovers and presses (SDD 22 section 2.4)
        Interaction->SetIconHits(mIconLayer.GetHits(), frame.SizePx, mUiScale);
    }
}

//////////////////////////////////////////////////////////////////////////
// Draws the lane tunnels' wall labels: projects the anchors and dot rows, places, eases and draws the glow text
void ASOLFlightHud::DrawLaneTunnelLabels(const FSOLHudView* view)
{
    SCOPE_CYCLE_COUNTER(STAT_SOL_HudLaneLabels);
    TRACE_CPUPROFILER_EVENT_SCOPE(ASOLFlightHud::DrawLaneTunnelLabels);
    mLastLaneLabelCount = 0;
    mLaneRowCount = 0;
    const double deltaS = FMath::Min(static_cast<double>(FApp::GetDeltaTime()), SOL::MAX_FRAME_DELTA_S);
    const double nowS = GetWorld()->GetRealTimeSeconds();
    for (FSOLLaneLabelCache& cache : mLaneLabels)
    {
        cache.TargetAlpha = 0.0;
        cache.PreviousCandidate = cache.Candidate;
        cache.Candidate = INDEX_NONE;
    }

    // The snapshot of this frame or the last one (a stale snapshot shows nothing and the labels ease out)
    const FSOLHudLaneTunnelLabels* snapshot = nullptr;
    if (view != nullptr && HudEvents != nullptr && AnchorSubsystem != nullptr && BodyRegistry != nullptr
        && HudEvents->GetBus().GetLaneTunnelLabels().Count > 0
        && GFrameCounter - HudEvents->GetBus().GetLaneTunnelLabelsFrame() <= SOL::NPC_TUNNEL_LABEL_MAX_AGE_FRAMES)
    {
        snapshot = &HudEvents->GetBus().GetLaneTunnelLabels();
    }
    if (snapshot != nullptr)
    {
        // Viewpoint-relative ecliptic metres to camera-local cm (true positions: the far-depth placement keeps every
        // point on its true ray, so a straight row stays straight on screen); the camera's true offset from the
        // viewpoint is its render offset inside the 1:1 zone
        const FVector3d viewpointM = AnchorSubsystem->GetViewpointM();
        const FVector cameraOffsetCm = view->Location - AnchorSubsystem->UniverseToRenderCm(viewpointM);
        const FVector3d cameraM = viewpointM
            + SOLRender::EclipticToUnreal(FVector3d(cameraOffsetCm)) / SOL::METERS_TO_CM;
        const auto toLocal = [&](const FVector3d& relM)
        {
            return view->Rotation.UnrotateVector(FVector(SOLRender::EclipticToUnreal(relM) * SOL::METERS_TO_CM)
                - cameraOffsetCm);
        };

        // The dot rows, clipped to the near plane and the viewport (grown by the label margin), then projected; each
        // row keeps the projected radius of its nearest on-screen dot (world-sized near dots grow to tens of pixels,
        // D11 keeps the labels off them), a conservative bound along the row
        const double pixelScale = HudLaneHeightScale(Canvas->ClipY);
        const double baseMarginPx = SOL::NPC_TUNNEL_LABEL_DOT_MARGIN_PX * mUiScale;
        const double floorDotPx = SOL::NPC_TUNNEL_DOT_SIZE_PX * pixelScale;
        const int32 rowCount = FMath::Clamp(snapshot->RowCount, 0, static_cast<int32>(SOL::NPC_TUNNEL_LABEL_MAX_ROWS));
        for (int32 row = 0; row < rowCount; ++row)
        {
            FVector a = toLocal(snapshot->RowStartM[row]);
            FVector b = toLocal(snapshot->RowEndM[row]);
            if (!HudClipSegmentToView(a, b, view->Center, view->FocalPx, Canvas->ClipX, Canvas->ClipY,
                HUD_NEAR_PLANE_CM, baseMarginPx))
            {
                continue;
            }
            ProjectLocal(*view, a, mLaneRowStarts[mLaneRowCount]);
            ProjectLocal(*view, b, mLaneRowEnds[mLaneRowCount]);
            mLaneRowDotRadiiPx[mLaneRowCount] = SOLLaneTunnelLabel::LaneLabelDotRadiusPx(FMath::Min(a.X, b.X),
                snapshot->RowDotDiameterM[row] * SOL::METERS_TO_CM, view->FocalPx, floorDotPx);
            ++mLaneRowCount;
        }

        // Each label: its text (reformatted only where it reads differently), size and the two projected anchors;
        // the compact size only when last frame's placement was compact (else after a full-form miss, below)
        const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
        FSOLLaneLabelInput inputs[SOL::NPC_TUNNEL_LABEL_MAX_COUNT];
        FSOLLaneLabelPlacement placements[SOL::NPC_TUNNEL_LABEL_MAX_COUNT];
        const float scale = mUiScale * SOL::NPC_TUNNEL_LABEL_TEXT_SCALE;
        const int32 count = FMath::Clamp(snapshot->Count, 0, static_cast<int32>(SOL::NPC_TUNNEL_LABEL_MAX_COUNT));
        bool bUsable[SOL::NPC_TUNNEL_LABEL_MAX_COUNT] = {};
        bool bAnyCandidate = false;
        for (int32 index = 0; index < count; ++index)
        {
            const FSOLHudLaneTunnelLabel& label = snapshot->Labels[index];
            FSOLLaneLabelInput& input = inputs[index];
            if (label.Lane < 0 || label.Lane >= SOL::NPC_GROUP_TEXTURE_WIDTH)
            {
                input.SizePx = FVector2D(-1.0, -1.0);
                continue;
            }

            // The anchors first; the occlusion test (a loop over the bodies) only for an anchor on screen, since one
            // off it places no box
            const FVector3d anchorsM[2] = { label.TopAnchorM, label.BottomAnchorM };
            for (int32 candidate = 0; candidate < 2; ++candidate)
            {
                FVector2D screen = FVector2D::ZeroVector;
                const bool bInFront = ProjectLocal(*view, toLocal(anchorsM[candidate]), screen);
                const bool bOnScreen = bInFront && HudIsOnViewport(screen, Canvas->ClipX, Canvas->ClipY);
                const bool bOccluded = bOnScreen && HudIsSegmentBlockedByBody(registry, cameraM,
                    viewpointM + anchorsM[candidate]);
                (candidate == 0 ? input.TopAnchorPx : input.BottomAnchorPx) = screen;
                (candidate == 0 ? input.bTopInFront : input.bBottomInFront) = bInFront;
                (candidate == 0 ? input.bTopOccluded : input.bBottomOccluded) = bOccluded;
                bUsable[index] |= bOnScreen && !bOccluded;
            }
            bAnyCandidate |= bUsable[index];

            // The text only for a label that can be placed (one easing out keeps its last text)
            FSOLLaneLabelCache& cache = mLaneLabels[label.Lane];
            if (bUsable[index])
            {
                UpdateLaneLabelText(cache, label, nowS);
            }
            input.SizePx = FVector2D(FMath::Max3(cache.Sizes[0].X, cache.Sizes[1].X, cache.Sizes[2].X),
                cache.Sizes[0].Y + cache.Sizes[1].Y + cache.Sizes[2].Y) * scale;
            input.PreviousCandidate = cache.PreviousCandidate;
            input.bPreviousCompact = cache.PreviousCandidate != INDEX_NONE && cache.bCompact;
            if (input.bPreviousCompact && bUsable[index])
            {
                UpdateLaneLabelCompactText(cache, label, nowS);
                input.CompactSizePx = cache.CompactSize * scale;
            }
        }

        // Greedy placement strongest first (last frame's spot, the full form, else the compact line), clear of every
        // dot row, of last frame's HUD panels and of the labels already placed; a label the full form could not
        // place gets its compact size and the placement runs again (at most once per label)
        // Skipped when no anchor is on screen, in front and unoccluded (no box can be placed, D19's views)
        for (int32 pass = 0; bAnyCandidate && pass <= count; ++pass)
        {
            SOLLaneTunnelLabel::PlaceLaneLabels(TConstArrayView<FSOLLaneLabelInput>(inputs, count),
                TConstArrayView<FVector2D>(mLaneRowStarts, mLaneRowCount),
                TConstArrayView<FVector2D>(mLaneRowEnds, mLaneRowCount),
                TConstArrayView<FBox2D>(mPrevHudAvoidBoxes, mPrevHudAvoidBoxCount),
                FVector2D(Canvas->ClipX, Canvas->ClipY), baseMarginPx, SOL::NPC_TUNNEL_LABEL_GAP_PX * mUiScale,
                TArrayView<FSOLLaneLabelPlacement>(placements, count),
                TConstArrayView<double>(mLaneRowDotRadiiPx, mLaneRowCount));
            bool bMeasured = false;
            for (int32 index = 0; index < count; ++index)
            {
                const FSOLHudLaneTunnelLabel& label = snapshot->Labels[index];
                if (!bUsable[index] || placements[index].bPlaced || inputs[index].CompactSizePx.X > 0.0
                    || label.Lane < 0 || label.Lane >= SOL::NPC_GROUP_TEXTURE_WIDTH)
                {
                    continue;
                }
                FSOLLaneLabelCache& cache = mLaneLabels[label.Lane];
                UpdateLaneLabelCompactText(cache, label, nowS);
                inputs[index].CompactSizePx = cache.CompactSize * scale;
                bMeasured |= inputs[index].CompactSizePx.X > 0.0;
            }
            if (!bMeasured)
            {
                break;
            }
        }
        for (int32 index = 0; index < count; ++index)
        {
            const FSOLHudLaneTunnelLabel& label = snapshot->Labels[index];
            if (placements[index].bPlaced && label.Lane >= 0 && label.Lane < SOL::NPC_GROUP_TEXTURE_WIDTH)
            {
                FSOLLaneLabelCache& cache = mLaneLabels[label.Lane];
                cache.TargetAlpha = label.Alpha;
                cache.Box = placements[index].BoxPx;
                cache.bCompact = placements[index].bCompact;
                cache.Candidate = placements[index].Candidate;
            }
        }
    }

    // Every lane's alpha eases toward its target; a label still visible draws at its last box
    const float scale = mUiScale * SOL::NPC_TUNNEL_LABEL_TEXT_SCALE;
    for (int32 lane = 0; lane < SOL::NPC_GROUP_TEXTURE_WIDTH; ++lane)
    {
        FSOLLaneLabelCache& cache = mLaneLabels[lane];
        if (cache.Alpha <= 0.0 && cache.TargetAlpha <= 0.0)
        {
            continue;
        }
        cache.Alpha = SOLLaneTunnelLabel::EaseLaneLabelAlpha(cache.Alpha, cache.TargetAlpha, deltaS);
        if (view == nullptr || cache.Alpha <= 0.0 || !cache.bHasText || !cache.Box.bIsValid)
        {
            continue;
        }
        FLinearColor color = HUD_PRIMARY;
        color.A *= static_cast<float>(cache.Alpha);
        if (cache.bCompact && !cache.bHasCompactText)
        {
            continue;
        }

        // The three lines, or the one compact line, centred in the box from its top
        float y = static_cast<float>(cache.Box.Min.Y);
        const float centerX = static_cast<float>(cache.Box.GetCenter().X);
        const int32 lineCount = cache.bCompact ? 1 : 3;
        for (int32 line = 0; line < lineCount; ++line)
        {
            const FText& text = cache.bCompact ? cache.CompactText : cache.Texts[line];
            const FVector2D& size = cache.bCompact ? cache.CompactSize : cache.Sizes[line];
            const FVector2f position(centerX - 0.5f * static_cast<float>(size.X) * scale, y);
            if (mTextList != nullptr)
            {
                mTextList->Add(text, position, scale, color, ESOLHudFont::Small);
            }
            else
            {
                FCanvasTextItem item(FVector2D(position), text, GetHudFont(ESOLHudFont::Small), color);
                item.Scale = FVector2D(scale, scale);
                item.EnableShadow(FLinearColor::Black);
                Canvas->DrawItem(item);
            }
            y += static_cast<float>(size.Y) * scale;
        }
        if (mLastLaneLabelCount < SOL::NPC_TUNNEL_LABEL_MAX_COUNT)
        {
            mLastLaneLabelLanes[mLastLaneLabelCount] = lane;
            mLastLaneLabelBoxes[mLastLaneLabelCount] = cache.Box;
            mLastLaneLabelAlphas[mLastLaneLabelCount] = static_cast<float>(cache.Alpha);
            mLastLaneLabelCompact[mLastLaneLabelCount] = cache.bCompact;
            ++mLastLaneLabelCount;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Updates a lane label's names and full text: each line reformatted and measured only when it reads differently
void ASOLFlightHud::UpdateLaneLabelText(FSOLLaneLabelCache& cache, const FSOLHudLaneTunnelLabel& label,
    const double nowS)
{
    // The body names, fetched only when the lane's bodies change
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    const bool bNamesChanged = !cache.bHasNames || cache.BodyA != label.BodyA || cache.BodyB != label.BodyB;
    if (bNamesChanged)
    {
        cache.NameA = label.BodyA >= 0 && label.BodyA < registry.Num() ? registry.GetName(label.BodyA).ToString()
            : FString();
        cache.NameB = label.BodyB >= 0 && label.BodyB < registry.Num() ? registry.GetName(label.BodyB).ToString()
            : FString();
        cache.BodyA = label.BodyA;
        cache.BodyB = label.BodyB;
        cache.bHasNames = true;
        cache.bHasCompactText = false;
    }

    // The lines when the displayed values change: at once for new names, the distances and the ship count checked at
    // most every NPC_TUNNEL_LABEL_TEXT_REFRESH_S (building the key formats both numbers, and a busy lane's count can
    // change every frame, so neither is done per frame); only a line that reads differently gets a new text and size
    const bool bUrgent = !cache.bHasText || bNamesChanged || label.Lane != cache.Key.Lane;
    if (!bUrgent && nowS - cache.KeyCheckS < SOL::NPC_TUNNEL_LABEL_TEXT_REFRESH_S)
    {
        return;
    }
    cache.KeyCheckS = nowS;
    const FSOLLaneLabelKey key = SOLLaneTunnelLabel::MakeLaneLabelKey(label.Lane, label.DistanceAM, label.DistanceBM,
        label.ShipCount);
    if (cache.bHasText && !bNamesChanged && cache.Key == key)
    {
        return;
    }
    SOLLaneTunnelLabel::FormatLaneLabelLines(cache.NameA, cache.NameB, label.DistanceAM, label.DistanceBM,
        label.ShipCount, mLaneLabelScratch[0], mLaneLabelScratch[1], mLaneLabelScratch[2]);
    for (int32 line = 0; line < 3; ++line)
    {
        if (cache.bHasText && cache.Lines[line].Equals(mLaneLabelScratch[line], ESearchCase::CaseSensitive))
        {
            continue;
        }
        cache.Lines[line] = mLaneLabelScratch[line];
        cache.Texts[line] = FText::FromString(cache.Lines[line]);
        float width = 0.0f;
        float height = 0.0f;
        Canvas->StrLen(GetHudFont(ESOLHudFont::Small), cache.Lines[line], width, height);
        cache.Sizes[line] = FVector2D(width, height);
    }
    cache.Key = key;
    cache.bHasText = true;
}

//////////////////////////////////////////////////////////////////////////
// Updates a lane label's compact line: its distance checked at most every NPC_TUNNEL_LABEL_TEXT_REFRESH_S, the line
// reformatted and measured only when it reads differently
void ASOLFlightHud::UpdateLaneLabelCompactText(FSOLLaneLabelCache& cache, const FSOLHudLaneTunnelLabel& label,
    const double nowS)
{
    if (cache.bHasCompactText && nowS - cache.CompactKeyCheckS < SOL::NPC_TUNNEL_LABEL_TEXT_REFRESH_S)
    {
        return;
    }
    cache.CompactKeyCheckS = nowS;
    const FSOLLaneLabelKey key = SOLLaneTunnelLabel::MakeLaneLabelCompactKey(label.Lane, label.DistanceAM,
        label.DistanceBM);
    if (cache.bHasCompactText && cache.CompactKey == key)
    {
        return;
    }
    SOLLaneTunnelLabel::FormatLaneLabelCompactLine(cache.NameA, cache.NameB, label.DistanceAM, label.DistanceBM,
        mLaneLabelScratch[0]);
    if (!cache.bHasCompactText || !cache.CompactLine.Equals(mLaneLabelScratch[0], ESearchCase::CaseSensitive))
    {
        cache.CompactLine = mLaneLabelScratch[0];
        cache.CompactText = FText::FromString(cache.CompactLine);
        float width = 0.0f;
        float height = 0.0f;
        Canvas->StrLen(GetHudFont(ESOLHudFont::Small), cache.CompactLine, width, height);
        cache.CompactSize = FVector2D(width, height);
    }
    cache.CompactKey = key;
    cache.bHasCompactText = true;
}

//////////////////////////////////////////////////////////////////////////
// Hides every lane label at once: nothing drawn, every alpha and candidate back to none
void ASOLFlightHud::HideLaneTunnelLabels()
{
    mLastLaneLabelCount = 0;
    for (FSOLLaneLabelCache& cache : mLaneLabels)
    {
        cache.Alpha = 0.0;
        cache.TargetAlpha = 0.0;
        cache.Candidate = INDEX_NONE;
        cache.PreviousCandidate = INDEX_NONE;
    }
}

//////////////////////////////////////////////////////////////////////////
// Records a HUD panel's screen box for next frame's lane labels to avoid
void ASOLFlightHud::AddHudAvoidBox(const FBox2D& box)
{
    if (box.bIsValid && mHudAvoidBoxCount < SOL::NPC_TUNNEL_LABEL_MAX_AVOID_BOXES)
    {
        mHudAvoidBoxes[mHudAvoidBoxCount++] = box;
    }
}

//////////////////////////////////////////////////////////////////////////
// Draws the interaction menus' HUD parts: the info card, the action hint and the open radial menu
void ASOLFlightHud::DrawInteraction()
{
    if (Interaction == nullptr)
    {
        return;
    }
    DrawInfoCard();

    // The Attack hint, below the screen centre
    if (Interaction->GetHintAction() == ESOLAction::Attack)
    {
        FSOLObjectInfo info;
        mText.Reset();
        mText.Append(SOLMenuText::INTERACT_ATTACK_HINT_PREFIX);
        if (Interaction->GetObjectInfo(Interaction->GetHintObject(), info) && !info.Name.IsNone())
        {
            info.Name.AppendString(mText);
        }
        mText.Append(SOLMenuText::INTERACT_ATTACK_HINT_SUFFIX);
        AddHoloPanel(DrawBufferCentered(ESOLHudLine::InteractHint, SOL::HudPalette::SECONDARY, Canvas->ClipX * 0.5f,
            Canvas->ClipY * SOL::Interact::HINT_Y_FRACTION, ESOLHudFont::Medium), SOL::HudPalette::SECONDARY);
    }

    // The radial menu (SDD 42), at the look this frame's update (DrawHUD) gave it: it closes from its cached state
    // after the interaction closed it
    if (mLastRadialLook.bVisible && !IsMapOpen())
    {
        DrawRadialMenu(mLastRadialLook, mLastRadialStrength);
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs the radial menu's anim for this frame, plays its cues and keeps the look to draw
void ASOLFlightHud::UpdateRadialMenu(const bool bShown)
{
    const float s = HoloMenu != nullptr ? HoloMenu->GetStrength() : SOL::Settings::HOLO_MENU_EFFECTS_DEFAULT;
    mLastRadialStrength = s;
    mLastRadialPadPx = SOLHoloFrameBuild::LinePad(s);
    mLastRadialHighlightIntensity = -1.0f;
    if (Interaction == nullptr)
    {
        mLastRadialLook = FSOLHoloMenuLook();
        return;
    }
    const double nowS = FPlatformTime::Seconds();
    const bool bIsContinuous = mLastRadialUpdateFrame != 0 && GFrameCounter <= mLastRadialUpdateFrame + 1;
    mLastRadialUpdateFrame = GFrameCounter;
    const FSOLHoloRadialCues cues = SOLHoloRadial::Update(mRadialHolo, Interaction->GetRadial(),
        Interaction->GetRadialActions(), Interaction->GetRadialOpenCount(), Interaction->GetRadialConfirmCount(),
        bShown, bIsContinuous, nowS, s);
    if (HoloMenu != nullptr)
    {
        // The cues play at every strength, 0 included (D21)
        if (cues.bOpen)
        {
            HoloMenu->PlayCue(ESOLHoloCue::Open, nowS);
        }
        if (cues.bHover)
        {
            HoloMenu->PlayCue(ESOLHoloCue::Hover, nowS);
        }
        if (cues.bSelect)
        {
            HoloMenu->PlayCue(ESOLHoloCue::Select, nowS);
        }
    }

    // The open menu's title, looked up once per opened menu (a body's info solves its orbit) and kept so the close
    // still shows it after the object went away
    const int32 openCount = Interaction->GetRadialOpenCount();
    if (Interaction->IsRadialOpen() && openCount != mRadialTitleOpenCount)
    {
        FSOLObjectInfo info;
        const bool bHasInfo = Interaction->GetObjectInfo(Interaction->GetRadial().Object, info);
        mRadialTitleName = bHasInfo ? info.Name : NAME_None;
        mRadialTitleType = bHasInfo ? info.Type : ESOLObjectType::Count;
        mRadialTitleOpenCount = openCount;
    }

    // The Canvas fallback draws the open menu whole, as before (no effects)
    if (mBatch == nullptr)
    {
        mLastRadialLook = FSOLHoloMenuLook();
        mLastRadialLook.bVisible = bShown && Interaction->IsRadialOpen();
        mLastRadialLook.Progress = 1.0f;
        mLastRadialLook.FrameFraction = 1.0f;
        mLastRadialLook.FillAlpha = 1.0f;
        return;
    }
    mLastRadialLook = SOLHoloMenu::Evaluate(mRadialHolo.Anim, nowS, s);
}

//////////////////////////////////////////////////////////////////////////
// Draws the radial menu from its cached state at a look: sectors, labels, title and the highlighted disabled action's
// reason, on the menu layer
void ASOLFlightHud::DrawRadialMenu(const FSOLHoloMenuLook& look, const float s)
{
    const FSOLRadialMenuState& radial = mRadialHolo.Radial;
    const FSOLActionList& actions = mRadialHolo.Actions;
    const int32 count = FMath::Min(radial.Count, actions.Num);
    if (count <= 0)
    {
        return;
    }
    const FVector2f center(radial.CenterPx);
    const float labelPx = SOL::Interact::RADIAL_LABEL_PX * mUiScale;
    const float halfAperture = count == 1 ? UE_PI
        : UE_PI / static_cast<float>(count) - 0.5f * SOL::Interact::RADIAL_SECTOR_GAP_RAD;

    // Sectors (LT's radial panel): thin arcs on the inner and outer rim with a gap between neighbours, grown from each
    // sector's middle while the menu draws in; the highlighted one in Secondary, brighter, glowing on hover and filled
    // (a faint wide arc), a disabled one dim (SOLHudSymbols::AppendRadialSector)
    for (int32 index = 0; mBatch != nullptr && index < count; ++index)
    {
        const bool bHighlighted = index == radial.Highlighted;
        const FSOLHoloRadialSectorLook sector = SOLHoloRadial::SectorLook(halfAperture, bHighlighted,
            actions.Entries[index].IsEnabled(), look, s);
        if (sector.HalfApertureRad <= 0.0f)
        {
            continue;
        }
        const float midAngle = static_cast<float>(SOLRadialMenu::SectorMidAngleRad(index, count));

        // A sector counts when every arc of it was taken (the batch only fills up)
        mSymbolScratch.Reset();
        const int32 built = SOLHudSymbols::AppendRadialSector(center, mUiScale, midAngle, sector, bHighlighted,
            mSymbolScratch);
        if (ReplaySymbolPrims(mSymbolScratch) == built)
        {
            ++mLastRadialSectorCount;
        }
        if (bHighlighted)
        {
            mLastRadialHighlightIntensity = sector.EdgeIntensity;
        }
    }
    if (look.FillAlpha <= 0.0f)
    {
        return;
    }

    // Labels on the label radius: right of the anchor on the right half, left of it on the left, centred near the
    // vertical; dim when disabled; all faded by the fill
    const float lineHeightPx = HUD_LINE_SMALL_PX * mUiScale;
    for (int32 index = 0; index < count; ++index)
    {
        const FSOLActionEntry& entry = actions.Entries[index];
        const ESOLHudLine line = static_cast<ESOLHudLine>(static_cast<int32>(ESOLHudLine::RadialItem0) + index);
        mText.Reset();
        mText.Append(SOLActionTable::ActionLabel(entry.Action));
        const double angle = SOLRadialMenu::SectorMidAngleRad(index, count);
        const double cosine = FMath::Cos(angle);
        const FVector2D anchor = radial.CenterPx + FVector2D(cosine, FMath::Sin(angle)) * labelPx;
        const float widthPx = static_cast<float>(MeasureLine(line, ESOLHudFont::Small).X) * mUiScale;
        const float x = cosine > SOL::Interact::LABEL_SIDE_COSINE ? static_cast<float>(anchor.X)
            : cosine < -SOL::Interact::LABEL_SIDE_COSINE ? static_cast<float>(anchor.X) - widthPx
            : static_cast<float>(anchor.X) - 0.5f * widthPx;
        MenuText(line, SOLHoloRadial::LabelColor(index == radial.Highlighted, entry.IsEnabled(), look.FillAlpha), x,
            static_cast<float>(anchor.Y) - 0.5f * lineHeightPx, ESOLHudFont::Small);
    }

    // Title (the object's name, else its type, as cached while open) above the ring; a highlighted disabled action's
    // reason below it
    mText.Reset();
    if (!mRadialTitleName.IsNone())
    {
        mRadialTitleName.AppendString(mText);
    }
    else if (mRadialTitleType != ESOLObjectType::Count)
    {
        mText.Append(SOLActionTable::TypeName(mRadialTitleType));
    }
    const float titleY = center.Y - labelPx - 0.5f * lineHeightPx
        - (HUD_LINE_MEDIUM_PX + SOL::Interact::TITLE_GAP_PX) * mUiScale;
    MenuTextCentered(ESOLHudLine::RadialTitle, SOLHoloRadial::Faded(SOL::HudPalette::PRIMARY, look.FillAlpha),
        center.X, titleY, ESOLHudFont::Medium);
    if (radial.Highlighted >= 0 && radial.Highlighted < count && !actions.Entries[radial.Highlighted].IsEnabled())
    {
        mText.Reset();
        mText.Append(SOLActionTable::BlockText(actions.Entries[radial.Highlighted].Block));
        MenuTextCentered(ESOLHudLine::RadialReason, SOLHoloRadial::Faded(SOL::HudPalette::SECONDARY, look.FillAlpha),
            center.X, center.Y + labelPx + lineHeightPx, ESOLHudFont::Small);
    }
}

//////////////////////////////////////////////////////////////////////////
// Draws the info card of the object Info was chosen for (top right): name, type, distance, size and status, as a
// frameless card that draws in with the open cue and closes from its cached object
void ASOLFlightHud::DrawInfoCard()
{
    // The live card's object and facts are cached; a closing card redraws the cached ones (refreshed while the object
    // still exists), so it closes after the card timed out or the object went away
    const FSOLObjectRef live = Interaction->GetInfoObject();
    FSOLObjectInfo liveInfo;
    const bool bLive = live.IsValid() && Interaction->GetObjectInfo(live, liveInfo);
    const bool bNewObject = bLive && !(live == mInfoCardObject);
    if (bLive)
    {
        mInfoCardObject = live;
        mInfoCardInfo = liveInfo;
    }
    else if (mInfoCardAnim.Phase != ESOLHoloMenuPhase::Closed && mInfoCardObject.IsValid()
        && Interaction->GetObjectInfo(mInfoCardObject, liveInfo))
    {
        mInfoCardInfo = liveInfo;
    }
    const bool bContinuous = mInfoCardFrame != 0 && GFrameCounter <= mInfoCardFrame + 1;
    mInfoCardFrame = GFrameCounter;
    if (HudCardUpdate(mInfoCardAnim, bLive, bNewObject, bContinuous, mCardNowS, mCardStrength) && HoloMenu != nullptr)
    {
        // Info chosen for a new object (or again while the card closed): the open cue, at every strength (D21)
        HoloMenu->PlayCue(ESOLHoloCue::Open, mCardNowS);
    }
    const FSOLHoloMenuLook look = CardLook(mInfoCardAnim, bLive);
    if (!look.bVisible)
    {
        return;
    }
    const FSOLObjectInfo& info = mInfoCardInfo;
    const FLinearColor secondary = SOLHoloRadial::Faded(SOL::HudPalette::SECONDARY, look.FillAlpha);
    const FLinearColor primary = SOLHoloRadial::Faded(SOL::HudPalette::PRIMARY, look.FillAlpha);
    const float x = Canvas->ClipX - (HUD_MARGIN_PX + SOL::Interact::INFO_CARD_WIDTH_PX) * mUiScale;
    float y = (HUD_MARGIN_PX + SOL::Interact::INFO_CARD_TOP_PX) * mUiScale;
    FBox2D panel(ForceInit);
    BeginReadoutGlow(ESOLHudCard::Info);

    // Name (or the type when the object has none), then the type with a moon's planet
    mText.Reset();
    if (!info.Name.IsNone())
    {
        info.Name.AppendString(mText);
    }
    else
    {
        mText.Append(SOLActionTable::TypeName(info.Type));
    }
    panel += MenuText(ESOLHudLine::InfoTitle, secondary, x, y, ESOLHudFont::Medium);
    y += HUD_LINE_MEDIUM_PX * mUiScale;
    mText.Reset();
    mText.Append(SOLActionTable::TypeName(info.Type));
    if (!info.ParentName.IsNone())
    {
        mText.Append(SOLMenuText::INTERACT_INFO_OF);
        info.ParentName.AppendString(mText);
    }
    panel += MenuText(ESOLHudLine::InfoType, primary, x, y, ESOLHudFont::Small);
    y += HUD_LINE_SMALL_PX * mUiScale;

    // Distance from the player ship (else the viewpoint), and the radius
    const FVector3d fromM = Ships != nullptr && Ships->HasPlayerShip() ? Ships->GetUniversePositionM()
        : AnchorSubsystem->GetViewpointM();
    mText.Reset();
    mText.Append(SOLMenuText::INTERACT_INFO_DISTANCE);
    SOLHudFormat::AppendDistanceM(mText, FVector3d::Dist(info.PositionM, fromM));
    panel += MenuText(ESOLHudLine::InfoDistance, primary, x, y, ESOLHudFont::Small);
    y += HUD_LINE_SMALL_PX * mUiScale;
    int32 lines = 3;
    if (info.RadiusM > 0.0)
    {
        mText.Reset();
        mText.Append(SOLMenuText::INTERACT_INFO_RADIUS);
        SOLHudFormat::AppendDistanceM(mText, info.RadiusM);
        panel += MenuText(ESOLHudLine::InfoDetail, primary, x, y, ESOLHudFont::Small);
        y += HUD_LINE_SMALL_PX * mUiScale;
        ++lines;
    }

    // A dropped target's shield and hull
    if (info.ShieldFraction >= 0.0f)
    {
        mText.Reset();
        mText.Append(SOLMenuText::INTERACT_INFO_SHIELD);
        mText.AppendInt(FMath::RoundToInt32(100.0f * info.ShieldFraction));
        mText.Append(SOLMenuText::INTERACT_INFO_PERCENT);
        mText.Append(SOLMenuText::INTERACT_INFO_HULL);
        mText.AppendInt(FMath::RoundToInt32(100.0f * info.HealthFraction));
        mText.Append(SOLMenuText::INTERACT_INFO_PERCENT);
        panel += MenuText(ESOLHudLine::InfoStatus, primary, x, y, ESOLHudFont::Small);
        ++lines;
    }
    EndReadoutGlow();
    DrawCard(ESOLHudCard::Info, panel, look);
    mLastInfoCardLineCount = lines;
}

//////////////////////////////////////////////////////////////////////////
// Draws the prograde and retrograde markers of the velocity relative to the active frame
void ASOLFlightHud::DrawVelocityMarkers(const FSOLHudView& view, const FSOLShipState& state)
{
    const FVector3d relativeVelocity = state.VelocityMps - Ships->GetReferenceVelocityMps();
    if (relativeVelocity.Size() < SOL::Hologram::HUD_VELOCITY_MARKER_MIN_MPS)
    {
        return;
    }

    // The Unreal-handed universe frame has Unreal's axes, so the velocity direction is a render-space direction
    const FVector local = view.Rotation.UnrotateVector(relativeVelocity.GetUnsafeNormal() * HUD_DIRECTION_PROJECT_CM);
    const float radius = SOL::HudGeometry::VELOCITY_MARKER_RADIUS_PX * mUiScale;
    FVector2D screen;

    // Prograde (LT Good): a circle with three ticks (left, right, up)
    if (ProjectLocal(view, local, screen) && IsOnScreen(screen, radius))
    {
        mSymbolScratch.Reset();
        SOLHudSymbols::AppendPrograde(screen, mUiScale, mSymbolScratch);
        ReplaySymbolPrims(mSymbolScratch);
    }

    // Retrograde (LT Hostile): a circle with a cross
    if (ProjectLocal(view, -local, screen) && IsOnScreen(screen, radius))
    {
        mSymbolScratch.Reset();
        SOLHudSymbols::AppendRetrograde(screen, mUiScale, mSymbolScratch);
        ReplaySymbolPrims(mSymbolScratch);
    }
}

//////////////////////////////////////////////////////////////////////////
// Builds the radar contacts, picks the range and draws the hologram scope on the ship-horizontal plane (bottom right)
void ASOLFlightHud::DrawRadar(const FSOLHudView* view, const FSOLShipState& state)
{
    // Contacts in the ship-horizontal frame (X forward, Y right, Z ship-up), rebuilt in place (capacity kept)
    const TConstArrayView<FSOLTargetInfo> candidates = Targeting->GetCandidates();
    const int32 selectedIndex = Targeting->GetSelectedIndex();
    mRadarContacts.Reset();
    for (int32 index = 0; index < candidates.Num(); ++index)
    {
        FSOLRadarContact& contact = mRadarContacts.AddDefaulted_GetRef();
        contact.RelativePositionM = SOLRadar::ToShipFrame(candidates[index].PositionM, state.PositionM,
            state.Orientation);
        contact.Name = candidates[index].Name;
        contact.bSelected = index == selectedIndex;
    }

    // Ships on Radar (SDD 22): the NPC ships the icon layer chose this frame (nearest first, a hard cap)
    mLastRadarShipCount = 0;
    if (mAreShipsSelected && Npcs != nullptr && (Settings == nullptr
        || Settings->IsOverlayShown(SOLOverlay::EOverlay::ShipsOnRadar)))
    {
        static const FName shipContactName(TEXT("Ship"));
        const TConstArrayView<FSOLNpcCpuShip> ships = Npcs->GetCpuShips();
        for (const int32 ship : mIconLayer.GetSelectedShips())
        {
            if (!ships.IsValidIndex(ship) || mRadarContacts.Num() >= mRadarContacts.Max())
            {
                continue;
            }
            FSOLRadarContact& contact = mRadarContacts.AddDefaulted_GetRef();
            contact.RelativePositionM = SOLRadar::ToShipFrame(SOLRender::EclipticToUnreal(ships[ship].PositionM),
                state.PositionM, state.Orientation);
            contact.Name = shipContactName;
            ++mLastRadarShipCount;
        }
    }

    // AUTO: the AUTO range for the current target state; MANUAL: the zoomed range, independent of the target (SDD 36
    // D4, D5: StepManualRange already clamps it to [MANUAL_FLOOR_M, MaxRangeM])
    const double rangeM = mIsRadarManual ? mManualRadarRangeM : SOLRadar::AutoRangeM(HasSelectedTarget());
    mLastRadarRangeM = rangeM;

    // Scope layout: the plane is an ellipse (the tilted disc); below it the longest downward stem (plus half a
    // selected blip) is reserved so no blip reaches the range label or the screen edge
    const float radius = SOL::HudGeometry::RADAR_RADIUS_PX * mUiScale;
    const float labelHeight = HUD_LINE_SMALL_PX * mUiScale;
    const float squashedRadius = radius * SOL::HudGeometry::RADAR_TILT;
    const float stalkReserve = radius * SOL::HudGeometry::RADAR_STALK_SCALE
        + 0.5f * SOLHudSymbols::RadarBlipPx(true, mUiScale);
    const float rightEdge = Canvas->ClipX - HUD_MARGIN_PX * mUiScale;
    const FVector2D center(rightEdge - radius,
        Canvas->ClipY - HUD_MARGIN_PX * mUiScale - labelHeight - stalkReserve - squashedRadius);
    const FVector2D labelPos(center.X - radius, center.Y + squashedRadius + stalkReserve);

    // The scope with its upward stems and the range label, kept clear of the lane labels
    AddHudAvoidBox(FBox2D(FVector2D(center.X - radius,
        center.Y - squashedRadius - radius * SOL::HudGeometry::RADAR_STALK_SCALE),
        FVector2D(rightEdge, labelPos.Y + labelHeight)));
    if (mBatch == nullptr)
    {
        DrawRect(HUD_RADAR_BACK_COLOR, center.X - radius, center.Y - squashedRadius, 2.0f * radius,
            2.0f * squashedRadius);
    }

    // The scope: range rings with LT's sonar pulse travelling outward (strength = the HUD effects slider), the axes
    // and ticks, the view cone (the camera's horizontal field of view around its heading on the plane; free look
    // turns it) and the ship chevron
    FSOLRadarScopeParams scope;
    scope.Center = center;
    scope.UiScale = mUiScale;
    scope.TimeS = GetWorld()->GetRealTimeSeconds();
    scope.Strength = FMath::Max(mShapeStrength, 0.0f);
    if (view != nullptr)
    {
        const FVector3d cameraForward = SOLRadar::ToShipFrame(FVector3d(view->Rotation.GetForwardVector()),
            FVector3d::ZeroVector, state.Orientation);
        scope.ConeHeadingRad = SOLRadar::ShipAzimuth(cameraForward);
        scope.ConeHalfFovRad = FMath::Atan2(0.5 * Canvas->ClipX, FMath::Max(view->FocalPx, 1.0));

        // Looking almost straight up or down the plane: the heading is undefined, no cone
        scope.bHasCone = FVector2D(cameraForward.X, cameraForward.Y).Length() >= HUD_RADAR_CONE_MIN_LATERAL;
    }
    mSymbolScratch.Reset();
    SOLHudSymbols::AppendRadarScope(scope, mSymbolScratch);
    ReplaySymbolPrims(mSymbolScratch);

    // Contacts within the budget (the selected one first, then the nearest): a foot tick on the plane, a stem to the
    // contact's height and a blip; above the plane solid and filled in Primary, below dashed and hollow in Secondary
    // The whole order is kept so a crowded (skipped) blip does not use up the budget: drawing stops at the budget
    SOLRadar::SelectContacts(mRadarContacts, mRadarContacts.Num(), mRadarOrder);
    mLastRadarAboveCount = 0;
    mLastRadarBelowCount = 0;
    UFont* small = GetHudFont(ESOLHudFont::Small);
    FVector2D drawnTops[SOLRadar::MaxDrawnContacts];
    int32 drawnCount = 0;
    const double mergePx = HUD_RADAR_MERGE_PX * mUiScale;
    for (const int32 contactIndex : mRadarOrder)
    {
        if (drawnCount >= SOLRadar::MaxDrawnContacts)
        {
            break;
        }
        const FSOLRadarContact& contact = mRadarContacts[contactIndex];
        const FSOLRadarPoint point = SOLRadar::ProjectContact(contact, rangeM);
        const FSOLRadarStyle style = SOLRadar::ContactStyle(point);
        const bool bAbove = style.Side == ESOLRadarSide::Above;
        const FLinearColor& color = bAbove ? HUD_PRIMARY : HUD_SECONDARY;
        const bool bBeyond = contact.RelativePositionM.SquaredLength() > rangeM * rangeM;
        const float intensity = style.bLarge ? 1.0f
            : ((point.bBehind ? HUD_RADAR_BEHIND_INTENSITY : 1.0f) * (bBeyond ? HUD_RADAR_BEYOND_INTENSITY : 1.0f));
        const FVector2D foot = SOLRadar::ScopeToScreen(center, radius, SOL::HudGeometry::RADAR_TILT,
            point.ScreenOffsetUnit);
        const FVector2D top = SOLRadar::StemTop(foot, point.HeightStalkUnit, radius,
            SOL::HudGeometry::RADAR_STALK_SCALE);

        // A blip on top of one already drawn (moons stacked on their planet) is skipped; the selected one comes first
        if (SOLRadar::IsCrowded(TConstArrayView<FVector2D>(drawnTops, drawnCount), top, mergePx))
        {
            continue;
        }
        drawnTops[drawnCount++] = top;

        ++(bAbove ? mLastRadarAboveCount : mLastRadarBelowCount);
        mSymbolScratch.Reset();
        SOLHudSymbols::AppendRadarContact(foot, top, style, intensity, mUiScale, mSymbolScratch);
        ReplaySymbolPrims(mSymbolScratch);
        if (style.bLarge)
        {
            // The name to the right of the blip, flipped to its left at the screen edge
            const float blip = SOLHudSymbols::RadarBlipPx(true, mUiScale);
            mText.Reset();
            point.Name.AppendString(mText);
            float labelWidth = 0.0f;
            float labelTall = 0.0f;
            GetTextSize(mText, labelWidth, labelTall, small, mUiScale);
            const float labelX = top.X + blip + labelWidth <= rightEdge ? top.X + blip : top.X - blip - labelWidth;
            DrawBuffer(ESOLHudLine::RadarSelected, color, labelX, top.Y - blip, ESOLHudFont::Small);
        }
    }

    // Range label below the reserved stem space, with the range mode
    mText.Reset();
    mText.Append(TEXT("RADAR "));
    SOLHudFormat::AppendDistanceM(mText, rangeM);
    mText.Append(mIsRadarManual ? TEXT(" (MANUAL)") : TEXT(" (AUTO)"));
    const FBox2D labelRect = DrawBuffer(ESOLHudLine::RadarRange, HudDimText(HUD_PRIMARY), labelPos.X, labelPos.Y,
        ESOLHudFont::Small);

    // A soft hologram panel behind the label, and a soft dark stadium-shaped backing under the disc (no box around the
    // radar, issue #31): the additive glow can only brighten, and over a sunlit planet the orange below-plane contacts
    // would otherwise wash out to white
    AddHoloPanel(labelRect, HUD_PRIMARY);
    if (mBatch != nullptr && mShapeLayer.IsValid())
    {
        const float grow = squashedRadius * HUD_RADAR_BACKING_GROW;
        mWasBackingDropped |= !mShapeLayer->AddBacking(FBox2f(FVector2f(center.X - radius - grow,
            center.Y - squashedRadius - grow), FVector2f(center.X + radius + grow, center.Y + squashedRadius + grow)),
            squashedRadius + grow, 2.0f * grow);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns true when a target is selected (sets the radar's AUTO range)
bool ASOLFlightHud::HasSelectedTarget() const
{
    return Targeting != nullptr && Targeting->GetSelectedTarget() != nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Appends a speed with its auto-picked unit to the text buffer
void ASOLFlightHud::AppendSpeed(const double speedMps)
{
    SOLHudFormat::AppendSpeedMps(mText, speedMps);
}
