/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLFlightHud.h"

#include "Flight/SOLNewtonianFlight.h"
#include "Game/SOLGameMode.h"
#include "HudEvents/SOLHudEventSubsystem.h"
#include "Level/SOLSurfaceLock.h"
#include "Map/SOLJumpSubsystem.h"
#include "Map/SOLMapModeSubsystem.h"
#include "Map/SOLMapPicking.h"
#include "Map/SOLRouteSubsystem.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "OrbitLines/SOLOrbitLineVisuals.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "UI/SOLHudFormat.h"
#include "UI/SOLHudShapeLayer.h"
#include "UI/SOLHudShapeMath.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
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
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RandomStream.h"

namespace
{
    // Layout at a 720-pixel-high canvas (scaled up with the canvas height, see HUD_REFERENCE_HEIGHT_PX)
    constexpr float HUD_REFERENCE_HEIGHT_PX = 720.0f;
    constexpr float HUD_MAX_UI_SCALE = 2.0f;
    constexpr float HUD_MARGIN_PX = 16.0f;
    constexpr float HUD_LINE_LARGE_PX = 36.0f;
    constexpr float HUD_SPEED_TEXT_SCALE = 1.8f;             // The main speed readout, on top of the UI scale
    constexpr float HUD_LINE_MEDIUM_PX = 20.0f;
    constexpr float HUD_LINE_SMALL_PX = 16.0f;
    constexpr float HUD_TARGET_PANEL_WIDTH_PX = 250.0f;
    constexpr float HUD_SPEED_BAR_WIDTH_PX = 240.0f;
    constexpr float HUD_SPEED_BAR_HEIGHT_PX = 8.0f;

    // Reticle and virtual joystick
    constexpr float HUD_RETICLE_GAP_PX = 6.0f;
    constexpr float HUD_RETICLE_ARM_PX = 12.0f;
    constexpr int32 HUD_CIRCLE_SEGMENTS = 48;
    constexpr float HUD_STICK_DOT_PX = 6.0f;

    // Target bracket and edge arrow
    constexpr float HUD_BRACKET_MIN_HALF_PX = 14.0f;
    constexpr float HUD_BRACKET_MAX_HALF_PX = 160.0f;
    constexpr float HUD_BRACKET_CORNER_FRACTION = 0.4f;
    constexpr float HUD_EDGE_ARROW_MARGIN_PX = 40.0f;
    constexpr float HUD_EDGE_ARROW_LENGTH_PX = 14.0f;
    constexpr float HUD_EDGE_ARROW_WIDTH_PX = 9.0f;

    // Prograde/retrograde markers: circle radius, shown above this relative speed
    constexpr float HUD_VELOCITY_MARKER_RADIUS_PX = 9.0f;
    constexpr double HUD_VELOCITY_MARKER_MIN_MPS = 0.1;
    constexpr double HUD_DIRECTION_PROJECT_CM = 1.0e6;   // Any distance works for a pure direction

    // Projection: points closer than this in front of the camera (cm) are clipped
    constexpr double HUD_NEAR_PLANE_CM = 10.0;

    // Radar scope (bottom right): radius, vertical squash of the tilted disc, stalk length per unit height
    constexpr float HUD_RADAR_RADIUS_PX = 105.0f;
    constexpr float HUD_RADAR_TILT = 0.5f;
    constexpr float HUD_RADAR_STALK_SCALE = 0.6f;
    constexpr float HUD_RADAR_BLIP_PX = 4.0f;
    constexpr float HUD_RADAR_SELECTED_BLIP_PX = 7.0f;
    constexpr float HUD_RADAR_INNER_RING = 0.5f;

    // LT palette (SDD 21 phase B). A tone is a palette colour and an intensity: glow shapes scale their glow by it,
    // Canvas lines (which ignore alpha) their RGB, and text lines take a dim tone as alpha (HudDimText)
    const FLinearColor& HUD_PRIMARY = SOL::HudPalette::PRIMARY;
    const FLinearColor& HUD_SECONDARY = SOL::HudPalette::SECONDARY;   // Warnings, the target, the locked frame
    const FLinearColor& HUD_GOOD = SOL::HudPalette::GOOD;             // Prograde marker
    const FLinearColor& HUD_HOSTILE = SOL::HudPalette::HOSTILE;       // Retrograde marker
    const FLinearColor& HUD_BAD = SOL::HudPalette::BAD;               // G critical / blackout / lethal (SDD 18)
    constexpr float HUD_DIM = 0.55f;                                    // Secondary lines (axes, ticks, stalks)
    constexpr float HUD_FAINT = 0.18f;                                  // The joystick circles
    constexpr float HUD_BRACKET_INTENSITY = 0.6f;                       // Target bracket and edge arrow (corners add)
    constexpr float HUD_STICK_DOT_INTENSITY = 0.6f;                     // The stick dot (a filled glow blob)
    constexpr float HUD_TEXT_DIM_ALPHA = 0.7f;                          // Dim text lines (hints, absolute speed)
    constexpr float HUD_BAR_TRACK_INTENSITY = 0.1f;                     // A glow bar's empty track
    constexpr float HUD_BAR_FILL_INTENSITY = 0.45f;                     // A glow bar's filled part
    constexpr float HUD_PANEL_GAP_PX = 8.0f;                            // Info panel to the key hints below it
    constexpr int32 HUD_VELOCITY_MARKER_SEGMENTS = 16;                  // Canvas fallback circle segments
    const FLinearColor HUD_BAR_BACK_COLOR(0.0f, 0.0f, 0.0f, 0.45f);
    constexpr float HUD_G_BLOCK_GAP_PX = 8.0f;                              // Gap between the G meter and speed block
    constexpr float HUD_G_WARNING_Y_FRACTION = 0.62f;                       // G warning: this far down the screen
    constexpr float HUD_AUTOPILOT_TOP_PX = 64.0f;                           // Autopilot marker below the top edge
    constexpr float HUD_ROUTE_HINT_GAP_PX = 2.0f;                           // Route marker gap below the key hints
    constexpr float HUD_STEER_ICON_RADIUS_PX = 6.0f;                        // Mouse-steer icon radius (SDD 19)
    constexpr float HUD_STEER_ICON_GAP_PX = 10.0f;                          // Gap between the mode line and the icon
    constexpr int32 HUD_STEER_ICON_SEGMENTS = 16;
    constexpr double HUD_CONTROLS_NOTICE_S = 15.0;                          // One-time controls notice duration
    constexpr float HUD_CONTROLS_NOTICE_Y_FRACTION = 0.2f;                  // Controls notice: this far down
    constexpr double HUD_PERCENT = 100.0;
    const FLinearColor HUD_RADAR_BACK_COLOR(0.0f, 0.02f, 0.05f, 0.35f);   // Radar (Canvas until phase C, #30)

    // Jump warp streaks: each travels from WARP_STREAK_START_FRACTION to past the corner (fractions of the half
    // diagonal) once per cycle, accelerating (squared phase), lengthening up to WARP_STREAK_MAX_LENGTH_FRACTION and
    // thickening up to 1 + WARP_STREAK_EXTRA_THICKNESS_PX; cycle rates vary per streak; fixed seed for a stable pattern
    constexpr double WARP_STREAK_START_FRACTION = 0.06;
    constexpr double WARP_STREAK_TRAVEL_FRACTION = 1.1;
    constexpr double WARP_STREAK_MIN_LENGTH_FRACTION = 0.04;
    constexpr double WARP_STREAK_MAX_LENGTH_FRACTION = 0.45;
    constexpr double WARP_STREAK_MIN_CYCLES_PER_S = 0.9;
    constexpr double WARP_STREAK_MAX_CYCLES_PER_S = 1.8;
    constexpr float WARP_STREAK_EXTRA_THICKNESS_PX = 2.0f;
    constexpr int32 WARP_STREAK_SEED = 2026;
    const FLinearColor WARP_STREAK_COLOR(0.8f, 0.9f, 1.0f, 1.0f);

    // Shape renderer (SDD 21): the reticle's glow dot radius, its layer under the menus and the F3 panel
    constexpr float HUD_RETICLE_DOT_RADIUS_PX = 1.0f;
    constexpr float HUD_RETICLE_DOT_INTENSITY = 0.5f;          // Dimmer than the arms: the centre stays readable
    constexpr int32 HUD_SHAPE_LAYER_Z_ORDER = 0;

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
    // Returns a text colour dimmed through its alpha
    FLinearColor HudDimText(const FLinearColor& color)
    {
        return FLinearColor(color.R, color.G, color.B, HUD_TEXT_DIM_ALPHA);
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
        mRadarContacts.Reserve(bodyCount + SOL::TARGETING_RESERVED_TARGETABLES);
        mMapBodyOverlay.Initialize(BodyRegistry->GetRegistry());
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
    }
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

    // The text lines use the same engine fonts and sizes Canvas draws them with (GEngine's fonts stay loaded)
    for (int32 index = 0; index < static_cast<int32>(ESOLHudFont::Count); ++index)
    {
        const ESOLHudFont font = static_cast<ESOLHudFont>(index);
        if (const UFont* engineFont = GetHudFont(font))
        {
            mShapeLayer->SetFont(font, engineFont->GetLegacySlateFontInfo());
        }
    }
    viewport->AddViewportWidgetContent(mShapeLayer.ToSharedRef(), HUD_SHAPE_LAYER_Z_ORDER);
    mShapeStrength = -1.0f;
    mShapeComposite = -1.0f;
    mShapeViewSize = FVector2f::ZeroVector;
}

//////////////////////////////////////////////////////////////////////////
// Removes the shape layer widget from the game viewport
void ASOLFlightHud::DestroyShapeLayer()
{
    if (mShapeLayer.IsValid())
    {
        UWorld* world = GetWorld();
        if (UGameViewportClient* viewport = world != nullptr ? world->GetGameViewport() : nullptr)
        {
            viewport->RemoveViewportWidgetContent(mShapeLayer.ToSharedRef());
        }
        mShapeLayer.Reset();
    }
    ShapeMaterial = nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Starts this frame's shape batch and text lines and pushes changed material parameters (strength, composite, size)
void ASOLFlightHud::BeginShapeFrame()
{
    mWasShapeRendererActive = false;
    mBatch = nullptr;
    mTextList = nullptr;
    mLastPanelCount = 0;
    if (!mShapeLayer.IsValid())
    {
        return;
    }

    // Warn once if the previous frame overflowed a fixed capacity (lines, shapes or backings silently disappear)
    const int32 droppedShapes = mShapeLayer->GetBatch().GetDroppedCount();
    const int32 droppedTexts = mShapeLayer->GetTexts().GetDroppedCount();
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
    if (ShapeMaterial == nullptr || Canvas == nullptr)
    {
        return;
    }
    mBatch = GetActiveShapeBatch();
    mTextList = mBatch != nullptr ? &mShapeLayer->GetTexts() : nullptr;
    mWasShapeRendererActive = mBatch != nullptr;
    mShapeLayer->SetTextGlow(CVarHudTextGlow.GetValueOnGameThread() != 0);

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
        const float halfWidth = FMath::Max(SOL::Hologram::HUD_LINE_HALF_WIDTH_PX, 0.5f * (thickness - 1.0f));
        mBatch->AddLine(FVector2f(a), FVector2f(b), halfWidth, color, intensity, SOL::Hologram::HUD_GLOW_PAD_PX);
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
            SOL::Hologram::HUD_GLOW_PAD_PX);
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
            mBatch->AddLine(start, end, halfWidth, color, intensity, SOL::Hologram::HUD_GLOW_PAD_PX);
        }
        return;
    }
    DrawRect(HudScaled(color, intensity), x, y, width, height);
}

//////////////////////////////////////////////////////////////////////////
// Draws a fill bar: a faint track, the filled part (clamped to [0, 1]) and an end tick
void ASOLFlightHud::DrawBar(const float x, const float y, const double fill01, const FLinearColor& color)
{
    const float width = HUD_SPEED_BAR_WIDTH_PX * mUiScale;
    const float height = HUD_SPEED_BAR_HEIGHT_PX * mUiScale;
    const float filled = width * static_cast<float>(FMath::Clamp(fill01, 0.0, 1.0));
    if (mBatch != nullptr)
    {
        GlowBox(x, y, width, height, HUD_PRIMARY, HUD_BAR_TRACK_INTENSITY);
    }
    else
    {
        DrawRect(HUD_BAR_BACK_COLOR, x, y, width, height);
    }
    GlowBox(x, y, filled, height, color, HUD_BAR_FILL_INTENSITY);
    GlowLine(FVector2D(x + width, y), FVector2D(x + width, y + height), HUD_PRIMARY, HUD_DIM, 1.0f);
}

//////////////////////////////////////////////////////////////////////////
// Queues a hologram panel (glow shape and dark backing) around a content rectangle (shape renderer only)
void ASOLFlightHud::AddHoloPanel(const FBox2D& content, const FLinearColor& color)
{
    if (mBatch == nullptr || !content.bIsValid || !mShapeLayer.IsValid())
    {
        return;
    }
    FVector2f center;
    FVector2f halfSize;
    SOLHudShapeMath::PanelAroundRect(FBox2f(FVector2f(content.Min), FVector2f(content.Max)),
        SOL::Hologram::HUD_PANEL_PADDING_PX * mUiScale, center, halfSize);
    if (mBatch->AddPanel(center, halfSize, color, SOL::Hologram::HUD_PANEL_EDGE_INTENSITY,
        SOL::Hologram::HUD_PANEL_FILL, SOL::Hologram::HUD_GLOW_PAD_PX))
    {
        mWasBackingDropped |= !mShapeLayer->AddBacking(FBox2f(center - halfSize, center + halfSize));
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
// Steps the radar range one decade in ('-') or out ('='); switches to MANUAL only when the range changes
void ASOLFlightHud::StepRadarZoom(const bool bZoomIn)
{
    // The first press starts from the range on screen; later presses from the manual range
    const double currentM = mIsRadarManual ? mManualRadarRangeM : mLastRadarRangeM;
    const double steppedM = SOLRadar::StepManualRange(currentM, bZoomIn, HasSelectedTarget());

    // A press that leaves the range where it is (e.g. '-' at the AUTO floor) keeps the current mode
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
    mLastSurfaceLockLine = ESOLSurfaceLockHudLine::None;
    mLastRouteLineText.Reset();
    mLastRouteLineRect = FBox2D(ForceInit);
    mLastHintsRect = FBox2D(ForceInit);
    mAutopilotBlockBottomY = 0.0f;
    mLeftBlockRect = FBox2D(ForceInit);
    mTopBlockRect = FBox2D(ForceInit);
    mRouteOverlay.ResetFrame();
    mHologramOverlay.ResetFrame();

    // The glow-shape batch is emptied first, so a frame that returns early below leaves nothing on screen
    BeginShapeFrame();
    if (Canvas == nullptr || SimClock == nullptr || BodyRegistry == nullptr || AnchorSubsystem == nullptr
        || GEngine == nullptr)
    {
        return;
    }
    const SOLSettingsModel::FSettingsValues* settings = Settings != nullptr ? &Settings->GetLiveValues() : nullptr;
    mUiScale = FMath::Clamp(Canvas->ClipY / HUD_REFERENCE_HEIGHT_PX, 1.0f, HUD_MAX_UI_SCALE)
        * (settings != nullptr ? settings->HudScale : 1.0f);

    // The main and pause menus (SDD 8) draw over a clean view: no flight HUD while one shows
    const USOLMenuSubsystem* menu = GetWorld()->GetSubsystem<USOLMenuSubsystem>();
    if (menu != nullptr && menu->IsMenuShowing())
    {
        return;
    }

    // Jump warp: the flight HUD is hidden for the whole sequence; only the radial streaks are drawn
    if (Jump != nullptr && Jump->IsWarping())
    {
        mLastWarpStreaksDrawn = DrawWarpStreaks(Jump->ComputeStreakIntensity(), Jump->GetElapsedS());
        return;
    }

    // The player hid the flight HUD (SDD 8): nothing more, unless the jump map needs its overlays
    if (settings != nullptr && !settings->bHudVisible && !IsMapOpen())
    {
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
            GEngine->GetSmallFont(), HUD_PRIMARY, mUiScale);
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
    DrawInfoBlock();

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

    // Text blocks gather their rectangles while drawing; each gets one hologram panel
    DrawSpeedBlock(state, control);
    DrawPilotBlock(control);
    DrawRouteMarker();
    AddHoloPanel(mLeftBlockRect, HUD_PRIMARY);
    AddHoloPanel(mTopBlockRect, HUD_PRIMARY);
    DrawRadar(state);

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
        const double innerPx = halfDiagonalPx * (WARP_STREAK_START_FRACTION
            + WARP_STREAK_TRAVEL_FRACTION * phase * phase);
        const double lengthPx = halfDiagonalPx * intensity * FMath::Lerp(WARP_STREAK_MIN_LENGTH_FRACTION,
            WARP_STREAK_MAX_LENGTH_FRACTION, phase);
        const FVector2D start = center + streak.Direction * innerPx;
        const FVector2D end = center + streak.Direction * (innerPx + lengthPx);
        const float brightness = static_cast<float>(intensity * (0.25 + 0.75 * phase));
        GlowLine(start, end, WARP_STREAK_COLOR, brightness, (1.0f + WARP_STREAK_EXTRA_THICKNESS_PX
            * static_cast<float>(phase)) * mUiScale);
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
// Draws the center reticle: four arms and a dimmer centre dot in LT Primary (glow shapes, or Canvas lines)
void ASOLFlightHud::DrawReticle(const FVector2D& center)
{
    const float gap = HUD_RETICLE_GAP_PX * mUiScale;
    const float arm = gap + HUD_RETICLE_ARM_PX * mUiScale;
    GlowLine(FVector2D(center.X - arm, center.Y), FVector2D(center.X - gap, center.Y), HUD_PRIMARY, 1.0f, 1.5f);
    GlowLine(FVector2D(center.X + gap, center.Y), FVector2D(center.X + arm, center.Y), HUD_PRIMARY, 1.0f, 1.5f);
    GlowLine(FVector2D(center.X, center.Y - arm), FVector2D(center.X, center.Y - gap), HUD_PRIMARY, 1.0f, 1.5f);
    GlowLine(FVector2D(center.X, center.Y + gap), FVector2D(center.X, center.Y + arm), HUD_PRIMARY, 1.0f, 1.5f);
    if (mBatch != nullptr)
    {
        mBatch->AddRing(FVector2f(center), HUD_RETICLE_DOT_RADIUS_PX * mUiScale, SOL::Hologram::HUD_LINE_HALF_WIDTH_PX,
            HUD_PRIMARY, HUD_RETICLE_DOT_INTENSITY, SOL::Hologram::HUD_GLOW_PAD_PX);
        return;
    }
    DrawRect(HUD_PRIMARY, center.X - 1.0f, center.Y - 1.0f, 2.0f, 2.0f);
}

//////////////////////////////////////////////////////////////////////////
// Draws the virtual-joystick circle, dead zone and the current stick offset
void ASOLFlightHud::DrawJoystick(const FVector2D& center, const FSOLHudPilotSnapshot& pilot)
{
    const float radius = static_cast<float>(pilot.StickRadiusPx);
    GlowRing(center, radius, HUD_PRIMARY, HUD_FAINT, 1.0f, HUD_CIRCLE_SEGMENTS);
    GlowRing(center, radius * static_cast<float>(SOL::JOYSTICK_DEAD_ZONE), HUD_PRIMARY, HUD_FAINT, 1.0f,
        HUD_CIRCLE_SEGMENTS / 2);

    // The stick offset is X right, Y up; the screen's Y grows downward
    const FVector2d& offset = pilot.StickOffsetPx;
    const FVector2D stick(center.X + offset.X, center.Y - offset.Y);
    const float intensity = (pilot.bIsFreeLooking || !pilot.bIsSteering ? HUD_DIM : 1.0f) * HUD_STICK_DOT_INTENSITY;
    if (offset.Size() > radius * SOL::JOYSTICK_DEAD_ZONE)
    {
        GlowLine(center, stick, HUD_PRIMARY, HUD_DIM, 1.0f);
    }
    const float dot = HUD_STICK_DOT_PX * mUiScale;
    GlowBox(stick.X - dot * 0.5f, stick.Y - dot * 0.5f, dot, dot, HUD_PRIMARY, intensity);
}

//////////////////////////////////////////////////////////////////////////
// Draws the sim date and warp, anchor, nearest body and altitude in a panel, and the key hints (top left)
void ASOLFlightHud::DrawInfoBlock()
{
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    const float x = HUD_MARGIN_PX * mUiScale;
    float y = HUD_MARGIN_PX * mUiScale;
    FBox2D panel(ForceInit);

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
    AddHoloPanel(panel, HUD_PRIMARY);

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
        + HUD_SPEED_BAR_HEIGHT_PX + 6.0f + HUD_LINE_SMALL_PX) * mUiScale;
    float y = Canvas->ClipY - HUD_MARGIN_PX * mUiScale - blockHeight;
    const double relativeSpeedMps = (state.VelocityMps - Ships->GetReferenceVelocityMps()).Size();

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
    DrawBar(x, y, fill, fill > 1.0 ? HUD_SECONDARY : HUD_PRIMARY);
    mLeftBlockRect += FBox2D(FVector2D(x, y), FVector2D(x + HUD_SPEED_BAR_WIDTH_PX * mUiScale,
        y + HUD_SPEED_BAR_HEIGHT_PX * mUiScale));
    y += (HUD_SPEED_BAR_HEIGHT_PX + 6.0f) * mUiScale;

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
        const FVector2D iconCenter(modeRect.Max.X + (HUD_STEER_ICON_GAP_PX + HUD_STEER_ICON_RADIUS_PX) * mUiScale,
            0.5 * (modeRect.Min.Y + modeRect.Max.Y));
        DrawSteerIcon(iconCenter, pilot.SteerIcon, pilot.bIsSteering);
        mLeftBlockRect += FBox2D(iconCenter - FVector2D(HUD_STEER_ICON_RADIUS_PX * mUiScale),
            iconCenter + FVector2D(HUD_STEER_ICON_RADIUS_PX * mUiScale));
    }
}

//////////////////////////////////////////////////////////////////////////
// Draws the mouse-steer icon: an open cursor arrow (Free), a reticle (Held) or a reticle with a lock tick (Locked)
void ASOLFlightHud::DrawSteerIcon(const FVector2D& center, const ESOLSteerIcon icon, const bool bSteering)
{
    const float radius = HUD_STEER_ICON_RADIUS_PX * mUiScale;
    const float intensity = bSteering ? 1.0f : HUD_DIM;
    if (icon == ESOLSteerIcon::Free)
    {
        // Cursor arrow: a tip at the top left, a vertical edge and a slanted edge closed by a short base
        const FVector2D tip(center.X - radius * 0.6f, center.Y - radius);
        const FVector2D bottom(tip.X, tip.Y + radius * 2.0f);
        const FVector2D side(tip.X + radius * 1.4f, tip.Y + radius * 1.4f);
        GlowLine(tip, bottom, HUD_PRIMARY, intensity, 1.5f);
        GlowLine(tip, side, HUD_PRIMARY, intensity, 1.5f);
        GlowLine(bottom, side, HUD_PRIMARY, intensity, 1.5f);
        return;
    }

    // Held and Locked: a reticle (circle, centre dot)
    GlowRing(center, radius, HUD_PRIMARY, intensity, 1.5f, HUD_STEER_ICON_SEGMENTS);
    GlowBox(center.X - 1.0f, center.Y - 1.0f, 2.0f, 2.0f, HUD_PRIMARY, intensity * HUD_STICK_DOT_INTENSITY);
    if (icon == ESOLSteerIcon::Locked)
    {
        // Lock tick: a small filled square on the lower right of the ring
        const float tick = radius * 0.7f;
        GlowBox(center.X + radius * 0.5f, center.Y + radius * 0.5f, tick, tick, HUD_PRIMARY, intensity);
    }
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
        + HUD_SPEED_BAR_HEIGHT_PX + 6.0f + HUD_LINE_SMALL_PX) * mUiScale;
    const float barHeight = HUD_SPEED_BAR_HEIGHT_PX * mUiScale;
    float y = Canvas->ClipY - HUD_MARGIN_PX * mUiScale - speedBlockHeight - HUD_G_BLOCK_GAP_PX * mUiScale
        - barHeight - HUD_LINE_SMALL_PX * mUiScale;
    const FLinearColor& meterColor = bAlarm ? HUD_BAD
        : (warning == ESOLGWarning::OverSustained ? HUD_SECONDARY : HUD_PRIMARY);
    mText.Reset();
    mText.Appendf(TEXT("G %.1f   tolerance %.0f%%"), gLoad, meter.Meter * HUD_PERCENT);
    mLeftBlockRect += DrawBuffer(ESOLHudLine::GMeter, meterColor, x, y, ESOLHudFont::Small);
    y += HUD_LINE_SMALL_PX * mUiScale;
    DrawBar(x, y, meter.Meter, meterColor);
    mLeftBlockRect += FBox2D(FVector2D(x, y), FVector2D(x + HUD_SPEED_BAR_WIDTH_PX * mUiScale, y + barHeight));

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

    // Autopilot marker with the ETA countdown (sim time), and the high-warp precision hint
    const int32 autopilotBody = Ships->GetAutopilotBodyIndex();
    if (autopilotBody == INDEX_NONE)
    {
        return;
    }
    const FSOLGuidanceOutput guidance = Ships->GetAutopilotGuidance();
    mText.Reset();
    mText.Append(TEXT("AUTOPILOT > "));
    BodyRegistry->GetRegistry().GetName(autopilotBody).AppendString(mText);
    mText.Append(TEXT("   ETA "));
    SOLHudFormat::AppendDurationS(mText, guidance.EtaS);
    mText.Append(TEXT("   "));
    SOLHudFormat::AppendDistanceM(mText, guidance.DistanceM);
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
    mTopBlockRect += DrawBuffer(ESOLHudLine::Autopilot, HUD_SECONDARY, centerX - halfWidth, top, ESOLHudFont::Medium);
    mText.Reset();
    mText.Append(SOLMenuText::AUTOPILOT_CANCEL_HINT);
    mTopBlockRect += DrawBufferCentered(ESOLHudLine::AutopilotCancel, HudDimText(HUD_PRIMARY), centerX,
        top + HUD_LINE_MEDIUM_PX * mUiScale, ESOLHudFont::Small);
    if (Ships->IsAutopilotPrecisionReduced())
    {
        mText.Reset();
        mText.Append(TEXT("WARP: autopilot precision reduced"));
        mTopBlockRect += DrawBufferCentered(ESOLHudLine::AutopilotHint, HUD_SECONDARY, centerX,
            top + (HUD_LINE_MEDIUM_PX + HUD_LINE_SMALL_PX) * mUiScale, ESOLHudFont::Small);
    }
    mAutopilotBlockBottomY = top + (HUD_LINE_MEDIUM_PX + HUD_LINE_SMALL_PX
        + (Ships->IsAutopilotPrecisionReduced() ? HUD_LINE_SMALL_PX : 0.0f)) * mUiScale;
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
    const float panelX = Canvas->ClipX - (HUD_MARGIN_PX + HUD_TARGET_PANEL_WIDTH_PX) * mUiScale;
    float panelY = HUD_MARGIN_PX * mUiScale;
    const FSOLTargetInfo* target = Targeting->GetSelectedTarget();
    const int32 selectedIndex = Targeting->GetSelectedIndex();
    mText.Reset();
    mText.Append(TEXT("TARGET  "));
    if (target == nullptr)
    {
        mText.Append(mKeyTexts.NoTarget);
        AddHoloPanel(DrawBuffer(ESOLHudLine::TargetName, HudDimText(HUD_PRIMARY), panelX, panelY,
            ESOLHudFont::Medium), HUD_PRIMARY);
        return;
    }

    // Readout panel (top right): name, distance, speed relative to the target
    const double distanceM = FVector3d::Dist(target->PositionM, state.PositionM);
    const double relativeSpeedMps = (state.VelocityMps - target->VelocityMps).Size();
    target->Name.AppendString(mText);
    FBox2D readout = DrawBuffer(ESOLHudLine::TargetName, HUD_SECONDARY, panelX, panelY, ESOLHudFont::Medium);
    panelY += HUD_LINE_MEDIUM_PX * mUiScale;
    mText.Reset();
    mText.Append(TEXT("DIST "));
    SOLHudFormat::AppendDistanceM(mText, distanceM);
    readout += DrawBuffer(ESOLHudLine::TargetDistance, HUD_SECONDARY, panelX, panelY, ESOLHudFont::Small);
    panelY += HUD_LINE_SMALL_PX * mUiScale;
    mText.Reset();
    mText.Append(TEXT("REL "));
    AppendSpeed(relativeSpeedMps);
    readout += DrawBuffer(ESOLHudLine::TargetRelative, HUD_SECONDARY, panelX, panelY, ESOLHudFont::Small);
    AddHoloPanel(readout, HUD_SECONDARY);

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
        renderCm = AnchorSubsystem->ComputePointRenderLocationCm(target->PositionM);
        radiusCm = target->RadiusM * SOL::METERS_TO_CM;
    }
    const FVector local = view.Rotation.UnrotateVector(renderCm - view.Location);
    FVector2D screen;
    if (ProjectLocal(view, local, screen) && IsOnScreen(screen, HUD_EDGE_ARROW_MARGIN_PX * mUiScale))
    {
        // Corner bracket sized to the target's apparent radius, with its name and distance beneath
        const float half = FMath::Clamp(static_cast<float>(view.FocalPx * radiusCm / local.X),
            HUD_BRACKET_MIN_HALF_PX * mUiScale, HUD_BRACKET_MAX_HALF_PX * mUiScale);
        const float corner = half * HUD_BRACKET_CORNER_FRACTION;
        for (int32 cornerIndex = 0; cornerIndex < 4; ++cornerIndex)
        {
            const float sx = (cornerIndex & 1) ? 1.0f : -1.0f;
            const float sy = (cornerIndex & 2) ? 1.0f : -1.0f;
            const FVector2D tip(screen.X + sx * half, screen.Y + sy * half);
            GlowLine(tip, FVector2D(tip.X - sx * corner, tip.Y), HUD_SECONDARY, HUD_BRACKET_INTENSITY, 1.5f);
            GlowLine(tip, FVector2D(tip.X, tip.Y - sy * corner), HUD_SECONDARY, HUD_BRACKET_INTENSITY, 1.5f);
        }
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
    const FVector2D perpendicular(-direction.Y, direction.X);
    const FVector2D tip = anchor + direction * (HUD_EDGE_ARROW_LENGTH_PX * mUiScale);
    const FVector2D left = anchor + perpendicular * (HUD_EDGE_ARROW_WIDTH_PX * mUiScale);
    const FVector2D right = anchor - perpendicular * (HUD_EDGE_ARROW_WIDTH_PX * mUiScale);
    GlowLine(tip, left, HUD_SECONDARY, HUD_BRACKET_INTENSITY, 2.0f);
    GlowLine(tip, right, HUD_SECONDARY, HUD_BRACKET_INTENSITY, 2.0f);
    GlowLine(left, right, HUD_SECONDARY, HUD_BRACKET_INTENSITY, 2.0f);
    mText.Reset();
    target->Name.AppendString(mText);
    const FVector2D label = anchor - direction * (3.0f * HUD_EDGE_ARROW_LENGTH_PX * mUiScale);
    DrawBuffer(ESOLHudLine::TargetLabel, HUD_SECONDARY, label.X - 20.0f * mUiScale, label.Y - 8.0f * mUiScale,
        ESOLHudFont::Small);
}

//////////////////////////////////////////////////////////////////////////
// Draws the prograde and retrograde markers of the velocity relative to the active frame
void ASOLFlightHud::DrawVelocityMarkers(const FSOLHudView& view, const FSOLShipState& state)
{
    const FVector3d relativeVelocity = state.VelocityMps - Ships->GetReferenceVelocityMps();
    if (relativeVelocity.Size() < HUD_VELOCITY_MARKER_MIN_MPS)
    {
        return;
    }

    // The Unreal-handed universe frame has Unreal's axes, so the velocity direction is a render-space direction
    const FVector local = view.Rotation.UnrotateVector(relativeVelocity.GetUnsafeNormal() * HUD_DIRECTION_PROJECT_CM);
    const float radius = HUD_VELOCITY_MARKER_RADIUS_PX * mUiScale;
    FVector2D screen;

    // Prograde (LT Good): a circle with three ticks (left, right, up)
    if (ProjectLocal(view, local, screen) && IsOnScreen(screen, radius))
    {
        GlowRing(screen, radius, HUD_GOOD, 1.0f, 1.5f, HUD_VELOCITY_MARKER_SEGMENTS);
        GlowLine(FVector2D(screen.X - radius, screen.Y), FVector2D(screen.X - 2.0f * radius, screen.Y), HUD_GOOD,
            1.0f, 1.5f);
        GlowLine(FVector2D(screen.X + radius, screen.Y), FVector2D(screen.X + 2.0f * radius, screen.Y), HUD_GOOD,
            1.0f, 1.5f);
        GlowLine(FVector2D(screen.X, screen.Y - radius), FVector2D(screen.X, screen.Y - 2.0f * radius), HUD_GOOD,
            1.0f, 1.5f);
    }

    // Retrograde (LT Hostile): a circle with a cross
    if (ProjectLocal(view, -local, screen) && IsOnScreen(screen, radius))
    {
        const float arm = radius * 0.7f;
        GlowRing(screen, radius, HUD_HOSTILE, 1.0f, 1.5f, HUD_VELOCITY_MARKER_SEGMENTS);
        GlowLine(FVector2D(screen.X - arm, screen.Y - arm), FVector2D(screen.X + arm, screen.Y + arm), HUD_HOSTILE,
            1.0f, 1.5f);
        GlowLine(FVector2D(screen.X - arm, screen.Y + arm), FVector2D(screen.X + arm, screen.Y - arm), HUD_HOSTILE,
            1.0f, 1.5f);
    }
}

//////////////////////////////////////////////////////////////////////////
// Builds the radar contacts from the targeting candidates, picks the range and draws the scope (bottom right)
void ASOLFlightHud::DrawRadar(const FSOLShipState& state)
{
    // Contacts in the ship-local frame (X forward, Y right, Z up), rebuilt in place (capacity kept)
    const TConstArrayView<FSOLTargetInfo> candidates = Targeting->GetCandidates();
    const int32 selectedIndex = Targeting->GetSelectedIndex();
    mRadarContacts.Reset();
    for (int32 index = 0; index < candidates.Num(); ++index)
    {
        FSOLRadarContact& contact = mRadarContacts.AddDefaulted_GetRef();
        contact.RelativePositionM = state.Orientation.UnrotateVector(candidates[index].PositionM - state.PositionM);
        contact.Name = candidates[index].Name;
        contact.bSelected = index == selectedIndex;
    }

    // AUTO: the range floor for the current target state (SDD 2 Appendix C Amendment 3); MANUAL: the zoomed range,
    // reclamped to that floor
    const bool bHasTarget = HasSelectedTarget();
    const double floorM = SOLRadar::EffectiveFloorM(bHasTarget);
    if (mIsRadarManual)
    {
        mManualRadarRangeM = FMath::Clamp(mManualRadarRangeM, floorM, SOLRadar::MaxRangeM);
    }
    const double rangeM = mIsRadarManual ? mManualRadarRangeM : floorM;
    mLastRadarRangeM = rangeM;

    // Scope: a tilted disc (ellipse) with a half-range ring and the forward/right axes; below the disc it reserves
    // the longest downward stalk (plus half a selected blip) so no blip reaches the range label or the screen edge
    const float radius = HUD_RADAR_RADIUS_PX * mUiScale;
    const float labelHeight = HUD_LINE_SMALL_PX * mUiScale;
    const float squashedRadius = radius * HUD_RADAR_TILT;
    const float stalkReserve = radius * HUD_RADAR_STALK_SCALE + 0.5f * HUD_RADAR_SELECTED_BLIP_PX * mUiScale;
    const float rightEdge = Canvas->ClipX - HUD_MARGIN_PX * mUiScale;
    const FVector2D center(rightEdge - radius,
        Canvas->ClipY - HUD_MARGIN_PX * mUiScale - labelHeight - stalkReserve - squashedRadius);
    DrawRect(HUD_RADAR_BACK_COLOR, center.X - radius, center.Y - squashedRadius, 2.0f * radius,
        2.0f * squashedRadius);
    const FLinearColor radarDim = HudScaled(HUD_PRIMARY, HUD_DIM);
    DrawCircle(center, radius, HUD_PRIMARY, HUD_CIRCLE_SEGMENTS, HUD_RADAR_TILT, 1.5f);
    DrawCircle(center, radius * HUD_RADAR_INNER_RING, radarDim, HUD_CIRCLE_SEGMENTS, HUD_RADAR_TILT, 1.0f);
    DrawLine(center.X - radius, center.Y, center.X + radius, center.Y, radarDim, 1.0f);
    DrawLine(center.X, center.Y - squashedRadius, center.X, center.Y + squashedRadius, radarDim, 1.0f);
    DrawRect(HUD_PRIMARY, center.X - 2.0f, center.Y - 2.0f, 4.0f, 4.0f);

    // Blips: a stalk from the disc to the contact's height, the selected target larger and highlighted
    UFont* small = GetHudFont(ESOLHudFont::Small);
    for (const FSOLRadarContact& contact : mRadarContacts)
    {
        const FSOLRadarPoint point = SOLRadar::ProjectContact(contact, rangeM);
        const FVector2D base(center.X + point.ScreenOffsetUnit.X * radius,
            center.Y - point.ScreenOffsetUnit.Y * squashedRadius);
        const FVector2D top(base.X, base.Y - point.HeightStalkUnit * radius * HUD_RADAR_STALK_SCALE);
        const FLinearColor color = point.bSelected ? HUD_SECONDARY
            : (point.bBehind ? radarDim : HUD_PRIMARY);
        DrawLine(base.X, base.Y, top.X, top.Y, color, 1.0f);
        const float blip = (point.bSelected ? HUD_RADAR_SELECTED_BLIP_PX : HUD_RADAR_BLIP_PX) * mUiScale;
        DrawRect(color, top.X - blip * 0.5f, top.Y - blip * 0.5f, blip, blip);
        if (point.bSelected)
        {
            // Name to the right of the blip, flipped to its left when it would run past the screen edge
            mText.Reset();
            point.Name.AppendString(mText);
            float labelWidth = 0.0f;
            float labelTall = 0.0f;
            GetTextSize(mText, labelWidth, labelTall, small, mUiScale);
            const float labelX = top.X + blip + labelWidth <= rightEdge ? top.X + blip : top.X - blip - labelWidth;
            DrawBuffer(ESOLHudLine::RadarSelected, HUD_SECONDARY, labelX, top.Y - blip, ESOLHudFont::Small);
        }
    }

    // Range label below the reserved stalk space, with the range mode
    mText.Reset();
    mText.Append(TEXT("RADAR "));
    SOLHudFormat::AppendDistanceM(mText, rangeM);
    mText.Append(mIsRadarManual ? TEXT(" (MANUAL)") : TEXT(" (AUTO)"));
    DrawBuffer(ESOLHudLine::RadarRange, HudDimText(HUD_PRIMARY), center.X - radius,
        center.Y + squashedRadius + stalkReserve, ESOLHudFont::Small);
}

//////////////////////////////////////////////////////////////////////////
// Returns true when a target is selected (sets the radar's range floor)
bool ASOLFlightHud::HasSelectedTarget() const
{
    return Targeting != nullptr && Targeting->GetSelectedTarget() != nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Appends a speed with its auto-picked unit to the text buffer
void ASOLFlightHud::AppendSpeed(const double speedMps)
{
    const ESOLSpeedUnit unit = SOLHudFormat::PickSpeedUnit(speedMps);
    const double value = SOLHudFormat::ToUnitValue(speedMps, unit);
    switch (unit)
    {
    case ESOLSpeedUnit::KilometersPerSecond:
        mText.Appendf(TEXT("%.2f km/s"), value);
        break;
    case ESOLSpeedUnit::LightSpeed:
        mText.Appendf(TEXT("%.4f c"), value);
        break;
    case ESOLSpeedUnit::MetersPerSecond:
    default:
        mText.Appendf(TEXT("%.1f m/s"), value);
        break;
    }
}
