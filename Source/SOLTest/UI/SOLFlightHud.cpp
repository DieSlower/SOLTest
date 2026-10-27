/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLFlightHud.h"

#include "Flight/SOLNewtonianFlight.h"
#include "Game/SOLGameMode.h"
#include "Level/SOLSurfaceLock.h"
#include "Map/SOLJumpSubsystem.h"
#include "Map/SOLMapModeSubsystem.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "OrbitLines/SOLOrbitLineVisuals.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "UI/SOLHudFormat.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
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

    // Colors (canvas lines ignore alpha, so line colors are dimmed in RGB)
    const FLinearColor HUD_COLOR(0.55f, 0.95f, 0.65f, 0.95f);
    const FLinearColor HUD_DIM_COLOR(0.3f, 0.52f, 0.36f, 1.0f);
    const FLinearColor HUD_TEXT_COLOR(0.85f, 1.0f, 0.88f, 1.0f);
    const FLinearColor HUD_WARN_COLOR(1.0f, 0.55f, 0.25f, 1.0f);
    const FLinearColor HUD_TARGET_COLOR(1.0f, 0.85f, 0.3f, 1.0f);
    const FLinearColor HUD_PROGRADE_COLOR(0.4f, 1.0f, 0.4f, 0.95f);
    const FLinearColor HUD_RETROGRADE_COLOR(1.0f, 0.45f, 0.35f, 0.95f);
    const FLinearColor HUD_STICK_COLOR(0.22f, 0.24f, 0.23f, 1.0f);
    const FLinearColor HUD_BAR_BACK_COLOR(0.0f, 0.0f, 0.0f, 0.45f);
    const FLinearColor HUD_ALARM_COLOR(1.0f, 0.2f, 0.15f, 1.0f);           // G critical / blackout (SDD 18)
    constexpr float HUD_G_BLOCK_GAP_PX = 8.0f;                              // Gap between the G meter and speed block
    constexpr float HUD_G_WARNING_Y_FRACTION = 0.62f;                       // G warning: this far down the screen
    constexpr float HUD_AUTOPILOT_TOP_PX = 64.0f;                           // Autopilot marker below the top edge
    constexpr float HUD_STEER_ICON_RADIUS_PX = 6.0f;                        // Mouse-steer icon radius (SDD 19)
    constexpr float HUD_STEER_ICON_GAP_PX = 10.0f;                          // Gap between the mode line and the icon
    constexpr int32 HUD_STEER_ICON_SEGMENTS = 16;
    constexpr double HUD_CONTROLS_NOTICE_S = 15.0;                          // One-time controls notice duration
    constexpr float HUD_CONTROLS_NOTICE_Y_FRACTION = 0.2f;                  // Controls notice: this far down
    constexpr double HUD_PERCENT = 100.0;
    const FLinearColor HUD_RADAR_BACK_COLOR(0.0f, 0.05f, 0.02f, 0.35f);
    const FLinearColor HUD_RADAR_BEHIND_COLOR(0.3f, 0.52f, 0.36f, 1.0f);

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
    Settings = USOLSettingsSubsystem::Get(this);

    // Key hints follow the player's bindings: built now and again after each applied rebind, never per frame
    RebuildKeyHints();
    if (Settings != nullptr)
    {
        mKeyBindingsHandle = Settings->OnKeyBindingsChanged().AddUObject(this, &ASOLFlightHud::RebuildKeyHints);
    }

    // Buffers sized once so the draw path only resets and refills them
    mText.Reserve(256);

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
    Super::EndPlay(endPlayReason);
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
    if (bHasView && IsMapOpen())
    {
        // Jump map: bodies too small on screen get their icon and name label (SDD 3, sub-part 2c)
        FSOLMapOverlayView overlayView;
        overlayView.LocationCm = view.Location;
        overlayView.Rotation = view.Rotation;
        overlayView.Center = view.Center;
        overlayView.FocalPx = view.FocalPx;
        mMapBodyOverlay.Draw(*Canvas, overlayView, *MapMode, *AnchorSubsystem, BodyRegistry->GetRegistry(),
            GEngine->GetSmallFont(), HUD_TEXT_COLOR, mUiScale);

        // Destination pick (2d): reference ring, disc, height guide line and destination marker over the icons
        mMapPickOverlay.Draw(*Canvas, overlayView, *MapMode, *AnchorSubsystem, mUiScale);
    }
    DrawInfoBlock();

    const ASOLShipPawn* shipPawn = Cast<ASOLShipPawn>(GetOwningPawn());
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
        UFont* medium = GEngine->GetMediumFont();
        float textWidth = 0.0f;
        float textHeight = 0.0f;
        mText.Reset();
        mText.Append(mKeyTexts.ControlsChangedNotice);
        Canvas->StrLen(medium, mText, textWidth, textHeight);
        DrawBuffer(ESOLHudLine::ControlsNotice, HUD_TARGET_COLOR, Canvas->ClipX * 0.5f - textWidth * 0.5f * mUiScale,
            Canvas->ClipY * HUD_CONTROLS_NOTICE_Y_FRACTION, medium);
    }
    DrawSpeedBlock(state, control);
    DrawPilotBlock(control);
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
    if (shipPawn != nullptr)
    {
        DrawJoystick(center, *shipPawn);
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

    // Each streak streams outward from near the center and wraps around; canvas lines ignore alpha, so brightness
    // (intensity, and the streak's progress outward) scales the RGB instead
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
        FLinearColor color = WARP_STREAK_COLOR * brightness;
        color.A = 1.0f;
        DrawLine(start.X, start.Y, end.X, end.Y, color, (1.0f + WARP_STREAK_EXTRA_THICKNESS_PX
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

    // Horizontal FOV across the canvas width (the local player keeps the X FOV by default)
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
// Draws the text buffer as one HUD line at a position with a font and color, scaled by the UI scale times textScale
void ASOLFlightHud::DrawBuffer(const ESOLHudLine line, const FLinearColor& color, const float x, const float y,
    UFont* font, const float textScale)
{
    // Rebuild the line's FText only when its text changed (the FText is the one allocation left per changed line)
    FSOLHudLineCache& cache = mLines[static_cast<int32>(line)];
    if (!cache.Source.Equals(mText, ESearchCase::CaseSensitive))
    {
        cache.Source.Reset();
        cache.Source.Append(mText);
        cache.Text = FText::FromString(cache.Source);
    }

    // Canvas copies the FText by reference count; its own glyph layout inside DrawItem is out of our control
    FCanvasTextItem item(FVector2D(x, y), cache.Text, font, color);
    item.Scale = FVector2D(mUiScale * textScale, mUiScale * textScale);
    item.EnableShadow(FLinearColor::Black);
    Canvas->DrawItem(item);
}

//////////////////////////////////////////////////////////////////////////
// Draws the center reticle
void ASOLFlightHud::DrawReticle(const FVector2D& center)
{
    const float gap = HUD_RETICLE_GAP_PX * mUiScale;
    const float arm = gap + HUD_RETICLE_ARM_PX * mUiScale;
    DrawLine(center.X - arm, center.Y, center.X - gap, center.Y, HUD_COLOR, 1.5f);
    DrawLine(center.X + gap, center.Y, center.X + arm, center.Y, HUD_COLOR, 1.5f);
    DrawLine(center.X, center.Y - arm, center.X, center.Y - gap, HUD_COLOR, 1.5f);
    DrawLine(center.X, center.Y + gap, center.X, center.Y + arm, HUD_COLOR, 1.5f);
    DrawRect(HUD_COLOR, center.X - 1.0f, center.Y - 1.0f, 2.0f, 2.0f);
}

//////////////////////////////////////////////////////////////////////////
// Draws the virtual-joystick circle, dead zone and the current stick offset
void ASOLFlightHud::DrawJoystick(const FVector2D& center, const ASOLShipPawn& pawn)
{
    const float radius = static_cast<float>(pawn.GetStickRadiusPixels());
    DrawCircle(center, radius, HUD_STICK_COLOR, HUD_CIRCLE_SEGMENTS, 1.0f, 1.0f);
    DrawCircle(center, radius * static_cast<float>(SOL::JOYSTICK_DEAD_ZONE), HUD_STICK_COLOR, HUD_CIRCLE_SEGMENTS / 2,
        1.0f, 1.0f);

    // The stick offset is X right, Y up; the screen's Y grows downward
    const FVector2d& offset = pawn.GetStickOffsetPixels();
    const FVector2D stick(center.X + offset.X, center.Y - offset.Y);
    const FLinearColor color = pawn.IsFreeLooking() || !pawn.IsSteering() ? HUD_DIM_COLOR : HUD_COLOR;
    if (offset.Size() > radius * SOL::JOYSTICK_DEAD_ZONE)
    {
        DrawLine(center.X, center.Y, stick.X, stick.Y, HUD_DIM_COLOR, 1.0f);
    }
    const float dot = HUD_STICK_DOT_PX * mUiScale;
    DrawRect(color, stick.X - dot * 0.5f, stick.Y - dot * 0.5f, dot, dot);
}

//////////////////////////////////////////////////////////////////////////
// Draws the sim date and warp, anchor, nearest body and altitude, and the key hints (top left)
void ASOLFlightHud::DrawInfoBlock()
{
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    UFont* medium = GEngine->GetMediumFont();
    UFont* small = GEngine->GetSmallFont();
    const float x = HUD_MARGIN_PX * mUiScale;
    float y = HUD_MARGIN_PX * mUiScale;

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
    DrawBuffer(ESOLHudLine::DateWarp, warp > 1.0 ? HUD_WARN_COLOR : HUD_TEXT_COLOR, x, y, medium);
    y += HUD_LINE_MEDIUM_PX * mUiScale;

    // Anchor body
    const int32 anchorIndex = AnchorSubsystem->GetAnchorIndex();
    mText.Reset();
    mText.Append(TEXT("ANCHOR  "));
    if (anchorIndex != INDEX_NONE)
    {
        registry.GetName(anchorIndex).AppendString(mText);
    }
    DrawBuffer(ESOLHudLine::Anchor, HUD_TEXT_COLOR, x, y, medium);
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
    DrawBuffer(ESOLHudLine::Nearest, HUD_TEXT_COLOR, x, y, medium);
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
            DrawBuffer(ESOLHudLine::SurfaceLock, lock.bWarning ? HUD_WARN_COLOR : HUD_TEXT_COLOR, x, y, medium);
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
            DrawBuffer(ESOLHudLine::SurfaceLock, HUD_DIM_COLOR, x, y, medium);
            y += HUD_LINE_MEDIUM_PX * mUiScale;
        }
    }

    // Overlay state and key hints (the jump map's own keys while it is open)
    mText.Reset();
    if (IsMapOpen())
    {
        mText.Append(mMapHints);
        DrawBuffer(ESOLHudLine::Hints, HUD_TEXT_COLOR, x, y, small);
        y += HUD_LINE_SMALL_PX * mUiScale;

        // Destination picking (2d) and the jump (2e)
        mText.Reset();
        mText.Append(TEXT("PICK   left click body/space + drag: XY   hold Shift + move: height   Shift+click: lock"));
        mText.Append(TEXT("   X clear   Enter jump"));
        DrawBuffer(ESOLHudLine::MapPickHints, HUD_TEXT_COLOR, x, y, small);
        return;
    }
    mText.Append(AreOrbitLinesVisible() ? mFlightHintsOrbitsOn : mFlightHintsOrbitsOff);
    DrawBuffer(ESOLHudLine::Hints, HUD_DIM_COLOR, x, y, small);
}

//////////////////////////////////////////////////////////////////////////
// Draws the relative and absolute speed, the cap and its fill bar, assist and boost (bottom left)
void ASOLFlightHud::DrawSpeedBlock(const FSOLShipState& state, const FSOLShipControl& control)
{
    UFont* large = GEngine->GetLargeFont();
    UFont* medium = GEngine->GetMediumFont();
    UFont* small = GEngine->GetSmallFont();
    const float x = HUD_MARGIN_PX * mUiScale;
    const float blockHeight = (HUD_LINE_LARGE_PX + 2.0f * HUD_LINE_SMALL_PX + HUD_LINE_MEDIUM_PX
        + HUD_SPEED_BAR_HEIGHT_PX + 6.0f + HUD_LINE_SMALL_PX) * mUiScale;
    float y = Canvas->ClipY - HUD_MARGIN_PX * mUiScale - blockHeight;
    const double relativeSpeedMps = (state.VelocityMps - Ships->GetReferenceVelocityMps()).Size();

    // Speed relative to the active frame, large
    mText.Reset();
    AppendSpeed(relativeSpeedMps);
    DrawBuffer(ESOLHudLine::Speed, HUD_TEXT_COLOR, x, y, large, HUD_SPEED_TEXT_SCALE);
    y += HUD_LINE_LARGE_PX * mUiScale;

    // Frame name (locked target, else anchor) and the absolute Sun-frame speed, smaller
    mText.Reset();
    mText.Append(TEXT("rel "));
    Targeting->GetFrameName().AppendString(mText);
    if (Targeting->IsFrameLocked())
    {
        mText.Append(mKeyTexts.FrameLockTag);
    }
    DrawBuffer(ESOLHudLine::Frame, Targeting->IsFrameLocked() ? HUD_TARGET_COLOR : HUD_TEXT_COLOR, x, y, small);
    y += HUD_LINE_SMALL_PX * mUiScale;
    mText.Reset();
    mText.Append(TEXT("abs "));
    AppendSpeed(state.VelocityMps.Size());
    mText.Append(TEXT(" (Sun frame)"));
    DrawBuffer(ESOLHudLine::AbsoluteSpeed, HUD_DIM_COLOR, x, y, small);
    y += HUD_LINE_SMALL_PX * mUiScale;

    // Speed cap
    mText.Reset();
    mText.Append(TEXT("CAP "));
    AppendSpeed(control.SpeedCapMps);
    DrawBuffer(ESOLHudLine::Cap, HUD_TEXT_COLOR, x, y, medium);
    y += HUD_LINE_MEDIUM_PX * mUiScale;

    // Fill bar of the relative speed against the cap (orange above the cap, e.g. while boosting)
    const float barWidth = HUD_SPEED_BAR_WIDTH_PX * mUiScale;
    const float barHeight = HUD_SPEED_BAR_HEIGHT_PX * mUiScale;
    const double fill = control.SpeedCapMps > 0.0 ? relativeSpeedMps / control.SpeedCapMps : 0.0;
    DrawRect(HUD_BAR_BACK_COLOR, x, y, barWidth, barHeight);
    DrawRect(fill > 1.0 ? HUD_WARN_COLOR : HUD_COLOR, x, y, barWidth * static_cast<float>(FMath::Clamp(fill, 0.0, 1.0)),
        barHeight);
    DrawLine(x, y, x + barWidth, y, HUD_DIM_COLOR, 1.0f);
    DrawLine(x, y + barHeight, x + barWidth, y + barHeight, HUD_DIM_COLOR, 1.0f);
    DrawLine(x + barWidth, y, x + barWidth, y + barHeight, HUD_DIM_COLOR, 1.0f);
    y += barHeight + 6.0f * mUiScale;

    // Flight mode label in its color (SDD 19 section 3.7) and boost, then the mouse-steer icon to their right
    const ESOLFlightMode mode = Ships->GetFlightMode();
    mText.Reset();
    mText.Append(SOLHudFormat::FlightModeLabel(mode));
    mText.Append(control.bBoost ? TEXT("   BOOST") : TEXT("   boost off"));
    DrawBuffer(ESOLHudLine::Assist, SOLHudFormat::FlightModeColor(mode), x, y, small);
    if (const ASOLShipPawn* shipPawn = Cast<ASOLShipPawn>(GetOwningPawn()))
    {
        float textWidth = 0.0f;
        float textHeight = 0.0f;
        Canvas->StrLen(small, mText, textWidth, textHeight);
        const FVector2D iconCenter(x + (textWidth + HUD_STEER_ICON_GAP_PX + HUD_STEER_ICON_RADIUS_PX) * mUiScale,
            y + textHeight * 0.5f * mUiScale);
        DrawSteerIcon(iconCenter, shipPawn->GetSteerIcon(), shipPawn->IsSteering());
    }
}

//////////////////////////////////////////////////////////////////////////
// Draws the mouse-steer icon: an open cursor arrow (Free), a reticle (Held) or a reticle with a lock tick (Locked)
void ASOLFlightHud::DrawSteerIcon(const FVector2D& center, const ESOLSteerIcon icon, const bool bSteering)
{
    const float radius = HUD_STEER_ICON_RADIUS_PX * mUiScale;
    const FLinearColor color = bSteering ? HUD_COLOR : HUD_DIM_COLOR;
    if (icon == ESOLSteerIcon::Free)
    {
        // Cursor arrow: a tip at the top left, a vertical edge and a slanted edge closed by a short base
        const FVector2D tip(center.X - radius * 0.6f, center.Y - radius);
        const FVector2D bottom(tip.X, tip.Y + radius * 2.0f);
        const FVector2D side(tip.X + radius * 1.4f, tip.Y + radius * 1.4f);
        DrawLine(tip.X, tip.Y, bottom.X, bottom.Y, color, 1.5f);
        DrawLine(tip.X, tip.Y, side.X, side.Y, color, 1.5f);
        DrawLine(bottom.X, bottom.Y, side.X, side.Y, color, 1.5f);
        return;
    }

    // Held and Locked: a reticle (circle, centre dot)
    DrawCircle(center, radius, color, HUD_STEER_ICON_SEGMENTS, 1.0f, 1.5f);
    DrawRect(color, center.X - 1.0f, center.Y - 1.0f, 2.0f, 2.0f);
    if (icon == ESOLSteerIcon::Locked)
    {
        // Lock tick: a small filled square on the lower right of the ring
        const float tick = radius * 0.7f;
        DrawRect(color, center.X + radius * 0.5f, center.Y + radius * 0.5f, tick, tick);
    }
}

//////////////////////////////////////////////////////////////////////////
// Draws the pilot G meter (above the speed block), the G warning (under the reticle) and the autopilot marker
// with its ETA and the high-warp hint (top centre); SDD 18 placeholders, #19 owns the final layout
void ASOLFlightHud::DrawPilotBlock(const FSOLShipControl& control)
{
    UFont* medium = GEngine->GetMediumFont();
    UFont* small = GEngine->GetSmallFont();
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
    const float barWidth = HUD_SPEED_BAR_WIDTH_PX * mUiScale;
    const float barHeight = HUD_SPEED_BAR_HEIGHT_PX * mUiScale;
    float y = Canvas->ClipY - HUD_MARGIN_PX * mUiScale - speedBlockHeight - HUD_G_BLOCK_GAP_PX * mUiScale
        - barHeight - HUD_LINE_SMALL_PX * mUiScale;
    const FLinearColor meterColor = bAlarm ? HUD_ALARM_COLOR
        : (warning == ESOLGWarning::OverSustained ? HUD_WARN_COLOR : HUD_TEXT_COLOR);
    mText.Reset();
    mText.Appendf(TEXT("G %.1f   tolerance %.0f%%"), gLoad, meter.Meter * HUD_PERCENT);
    DrawBuffer(ESOLHudLine::GMeter, meterColor, x, y, small);
    y += HUD_LINE_SMALL_PX * mUiScale;
    DrawRect(HUD_BAR_BACK_COLOR, x, y, barWidth, barHeight);
    DrawRect(meterColor, x, y, barWidth * static_cast<float>(FMath::Clamp(meter.Meter, 0.0, 1.0)), barHeight);
    DrawLine(x + barWidth, y, x + barWidth, y + barHeight, HUD_DIM_COLOR, 1.0f);

    // Warning under the reticle: over the sustained limit only in manual flight (assist never exceeds it), the
    // critical meter and the blackout in every mode
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
        float textWidth = 0.0f;
        float textHeight = 0.0f;
        Canvas->StrLen(medium, mText, textWidth, textHeight);
        DrawBuffer(ESOLHudLine::GWarning, bAlarm ? HUD_ALARM_COLOR : HUD_WARN_COLOR,
            Canvas->ClipX * 0.5f - textWidth * 0.5f * mUiScale, Canvas->ClipY * HUD_G_WARNING_Y_FRACTION, medium);
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
    float textWidth = 0.0f;
    float textHeight = 0.0f;
    Canvas->StrLen(medium, mText, textWidth, textHeight);
    const float top = HUD_AUTOPILOT_TOP_PX * mUiScale;
    DrawBuffer(ESOLHudLine::Autopilot, HUD_TARGET_COLOR, Canvas->ClipX * 0.5f - textWidth * 0.5f * mUiScale, top,
        medium);
    mText.Reset();
    mText.Append(SOLMenuText::AUTOPILOT_CANCEL_HINT);
    Canvas->StrLen(small, mText, textWidth, textHeight);
    DrawBuffer(ESOLHudLine::AutopilotCancel, HUD_DIM_COLOR, Canvas->ClipX * 0.5f - textWidth * 0.5f * mUiScale,
        top + HUD_LINE_MEDIUM_PX * mUiScale, small);
    if (Ships->IsAutopilotPrecisionReduced())
    {
        mText.Reset();
        mText.Append(TEXT("WARP: autopilot precision reduced"));
        Canvas->StrLen(small, mText, textWidth, textHeight);
        DrawBuffer(ESOLHudLine::AutopilotHint, HUD_WARN_COLOR, Canvas->ClipX * 0.5f - textWidth * 0.5f * mUiScale,
            top + (HUD_LINE_MEDIUM_PX + HUD_LINE_SMALL_PX) * mUiScale, small);
    }
}

//////////////////////////////////////////////////////////////////////////
// Draws the selected target's bracket and readout, or an edge arrow when it is off-screen
void ASOLFlightHud::DrawTarget(const FSOLHudView& view, const FSOLShipState& state)
{
    UFont* medium = GEngine->GetMediumFont();
    UFont* small = GEngine->GetSmallFont();
    const float panelX = Canvas->ClipX - (HUD_MARGIN_PX + HUD_TARGET_PANEL_WIDTH_PX) * mUiScale;
    float panelY = HUD_MARGIN_PX * mUiScale;
    const FSOLTargetInfo* target = Targeting->GetSelectedTarget();
    const int32 selectedIndex = Targeting->GetSelectedIndex();
    mText.Reset();
    mText.Append(TEXT("TARGET  "));
    if (target == nullptr)
    {
        mText.Append(mKeyTexts.NoTarget);
        DrawBuffer(ESOLHudLine::TargetName, HUD_DIM_COLOR, panelX, panelY, medium);
        return;
    }

    // Readout panel (top right): name, distance, speed relative to the target
    const double distanceM = FVector3d::Dist(target->PositionM, state.PositionM);
    const double relativeSpeedMps = (state.VelocityMps - target->VelocityMps).Size();
    target->Name.AppendString(mText);
    DrawBuffer(ESOLHudLine::TargetName, HUD_TARGET_COLOR, panelX, panelY, medium);
    panelY += HUD_LINE_MEDIUM_PX * mUiScale;
    mText.Reset();
    mText.Append(TEXT("DIST "));
    SOLHudFormat::AppendDistanceM(mText, distanceM);
    DrawBuffer(ESOLHudLine::TargetDistance, HUD_TARGET_COLOR, panelX, panelY, small);
    panelY += HUD_LINE_SMALL_PX * mUiScale;
    mText.Reset();
    mText.Append(TEXT("REL "));
    AppendSpeed(relativeSpeedMps);
    DrawBuffer(ESOLHudLine::TargetRelative, HUD_TARGET_COLOR, panelX, panelY, small);

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
            DrawLine(tip.X, tip.Y, tip.X - sx * corner, tip.Y, HUD_TARGET_COLOR, 1.5f);
            DrawLine(tip.X, tip.Y, tip.X, tip.Y - sy * corner, HUD_TARGET_COLOR, 1.5f);
        }
        mText.Reset();
        target->Name.AppendString(mText);
        mText.Append(TEXT("  "));
        SOLHudFormat::AppendDistanceM(mText, distanceM);
        DrawBuffer(ESOLHudLine::TargetLabel, HUD_TARGET_COLOR, screen.X - half, screen.Y + half + 4.0f * mUiScale,
            small);
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
    DrawLine(tip.X, tip.Y, left.X, left.Y, HUD_TARGET_COLOR, 2.0f);
    DrawLine(tip.X, tip.Y, right.X, right.Y, HUD_TARGET_COLOR, 2.0f);
    DrawLine(left.X, left.Y, right.X, right.Y, HUD_TARGET_COLOR, 2.0f);
    mText.Reset();
    target->Name.AppendString(mText);
    const FVector2D label = anchor - direction * (3.0f * HUD_EDGE_ARROW_LENGTH_PX * mUiScale);
    DrawBuffer(ESOLHudLine::TargetLabel, HUD_TARGET_COLOR, label.X - 20.0f * mUiScale, label.Y - 8.0f * mUiScale,
        small);
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

    // Prograde: a circle with three ticks (left, right, up)
    if (ProjectLocal(view, local, screen) && IsOnScreen(screen, radius))
    {
        DrawCircle(screen, radius, HUD_PROGRADE_COLOR, 16, 1.0f, 1.5f);
        DrawLine(screen.X - radius, screen.Y, screen.X - 2.0f * radius, screen.Y, HUD_PROGRADE_COLOR, 1.5f);
        DrawLine(screen.X + radius, screen.Y, screen.X + 2.0f * radius, screen.Y, HUD_PROGRADE_COLOR, 1.5f);
        DrawLine(screen.X, screen.Y - radius, screen.X, screen.Y - 2.0f * radius, HUD_PROGRADE_COLOR, 1.5f);
    }

    // Retrograde: a circle with a cross
    if (ProjectLocal(view, -local, screen) && IsOnScreen(screen, radius))
    {
        const float arm = radius * 0.7f;
        DrawCircle(screen, radius, HUD_RETROGRADE_COLOR, 16, 1.0f, 1.5f);
        DrawLine(screen.X - arm, screen.Y - arm, screen.X + arm, screen.Y + arm, HUD_RETROGRADE_COLOR, 1.5f);
        DrawLine(screen.X - arm, screen.Y + arm, screen.X + arm, screen.Y - arm, HUD_RETROGRADE_COLOR, 1.5f);
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
    DrawCircle(center, radius, HUD_COLOR, HUD_CIRCLE_SEGMENTS, HUD_RADAR_TILT, 1.5f);
    DrawCircle(center, radius * HUD_RADAR_INNER_RING, HUD_DIM_COLOR, HUD_CIRCLE_SEGMENTS, HUD_RADAR_TILT, 1.0f);
    DrawLine(center.X - radius, center.Y, center.X + radius, center.Y, HUD_DIM_COLOR, 1.0f);
    DrawLine(center.X, center.Y - squashedRadius, center.X, center.Y + squashedRadius, HUD_DIM_COLOR, 1.0f);
    DrawRect(HUD_COLOR, center.X - 2.0f, center.Y - 2.0f, 4.0f, 4.0f);

    // Blips: a stalk from the disc to the contact's height, the selected target larger and highlighted
    UFont* small = GEngine->GetSmallFont();
    for (const FSOLRadarContact& contact : mRadarContacts)
    {
        const FSOLRadarPoint point = SOLRadar::ProjectContact(contact, rangeM);
        const FVector2D base(center.X + point.ScreenOffsetUnit.X * radius,
            center.Y - point.ScreenOffsetUnit.Y * squashedRadius);
        const FVector2D top(base.X, base.Y - point.HeightStalkUnit * radius * HUD_RADAR_STALK_SCALE);
        const FLinearColor color = point.bSelected ? HUD_TARGET_COLOR
            : (point.bBehind ? HUD_RADAR_BEHIND_COLOR : HUD_COLOR);
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
            DrawBuffer(ESOLHudLine::RadarSelected, HUD_TARGET_COLOR, labelX, top.Y - blip, small);
        }
    }

    // Range label below the reserved stalk space, with the range mode
    mText.Reset();
    mText.Append(TEXT("RADAR "));
    SOLHudFormat::AppendDistanceM(mText, rangeM);
    mText.Append(mIsRadarManual ? TEXT(" (MANUAL)") : TEXT(" (AUTO)"));
    DrawBuffer(ESOLHudLine::RadarRange, HUD_DIM_COLOR, center.X - radius, center.Y + squashedRadius + stalkReserve,
        small);
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
