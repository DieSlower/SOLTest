/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLFlightHud.h"

#include "Map/SOLMapModeSubsystem.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "UI/SOLHudFormat.h"
#include "UI/SOLOrbitLines.h"
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

    // Orbit lines: real seconds between re-samples while shown (the secular elements change slowly even under warp)
    constexpr double HUD_ORBIT_REFRESH_S = 5.0;

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
    const FLinearColor HUD_RADAR_BACK_COLOR(0.0f, 0.05f, 0.02f, 0.35f);
    const FLinearColor HUD_RADAR_BEHIND_COLOR(0.3f, 0.52f, 0.36f, 1.0f);
    const FLinearColor HUD_ORBIT_COLOR(0.25f, 0.42f, 0.75f, 1.0f);
    const FLinearColor HUD_ORBIT_TARGET_COLOR(0.85f, 0.72f, 0.25f, 1.0f);

    //////////////////////////////////////////////////////////////////////////
    // Clips a 2D segment to a rectangle (Liang-Barsky); false when nothing of it is inside
    bool FlightHudClipSegment(FVector2D& a, FVector2D& b, const FVector2D& minCorner, const FVector2D& maxCorner)
    {
        const FVector2D delta = b - a;
        const double p[4] = { -delta.X, delta.X, -delta.Y, delta.Y };
        const double q[4] = { a.X - minCorner.X, maxCorner.X - a.X, a.Y - minCorner.Y, maxCorner.Y - a.Y };
        double tStart = 0.0;
        double tEnd = 1.0;

        // Narrow [tStart, tEnd] against each of the four edges
        for (int32 edge = 0; edge < 4; ++edge)
        {
            if (p[edge] == 0.0)
            {
                if (q[edge] < 0.0)
                {
                    return false;
                }
                continue;
            }
            const double t = q[edge] / p[edge];
            if (p[edge] < 0.0)
            {
                tStart = FMath::Max(tStart, t);
            }
            else
            {
                tEnd = FMath::Min(tEnd, t);
            }
            if (tStart > tEnd)
            {
                return false;
            }
        }
        const FVector2D start = a;
        a = start + delta * tStart;
        b = start + delta * tEnd;
        return true;
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

    // Buffers sized once so the draw path only resets and refills them
    mText.Reserve(256);
    if (BodyRegistry != nullptr)
    {
        const int32 bodyCount = BodyRegistry->GetRegistry().Num();
        mOrbitPoints.SetNum(bodyCount);
        for (TArray<FVector3d>& points : mOrbitPoints)
        {
            points.Reserve(SOLOrbitLines::EllipseSegments + 1);
        }
        mRadarContacts.Reserve(bodyCount + SOL::TARGETING_RESERVED_TARGETABLES);
        mMapBodyOverlay.Initialize(BodyRegistry->GetRegistry());
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns true while the jump map is open (the view is the map camera, not the ship's)
bool ASOLFlightHud::IsMapOpen() const
{
    return MapMode != nullptr && MapMode->IsMapOpen();
}

//////////////////////////////////////////////////////////////////////////
// Toggles the body orbit ellipses (O); they are sampled again when turned on
void ASOLFlightHud::ToggleOrbitLines()
{
    mShowOrbitLines = !mShowOrbitLines;
    mHasOrbitSamples = false;
    UE_LOG(LogSOL, Log, TEXT("FlightHud %s: orbit lines %s"), *GetName(), mShowOrbitLines ? TEXT("ON") : TEXT("OFF"));
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
    mLastOrbitSegmentsDrawn = 0;
    if (Canvas == nullptr || SimClock == nullptr || BodyRegistry == nullptr || AnchorSubsystem == nullptr
        || GEngine == nullptr)
    {
        return;
    }
    mUiScale = FMath::Clamp(Canvas->ClipY / HUD_REFERENCE_HEIGHT_PX, 1.0f, HUD_MAX_UI_SCALE);

    FSOLHudView view;
    const bool bHasView = BuildView(view);

    // World-anchored overlays first so the panels draw on top of them
    if (bHasView && mShowOrbitLines)
    {
        DrawOrbitLines(view);
    }
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
    }
    DrawInfoBlock();

    const ASOLShipPawn* shipPawn = Cast<ASOLShipPawn>(GetOwningPawn());
    if (Ships == nullptr || Targeting == nullptr || !Ships->HasPlayerShip())
    {
        return;
    }
    const FSOLShipState state = Ships->GetState();
    const FSOLShipControl control = Ships->GetControl();
    DrawSpeedBlock(state, control);
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
    const FLinearColor color = pawn.IsFreeLooking() ? HUD_DIM_COLOR : HUD_COLOR;
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

    // Overlay state and key hints (the jump map's own keys while it is open)
    mText.Reset();
    if (IsMapOpen())
    {
        mText.Append(TEXT("JUMP MAP   right drag orbit   middle or Shift+right drag pan   wheel zoom   J / Esc close"));
        DrawBuffer(ESOLHudLine::Hints, HUD_TEXT_COLOR, x, y, small);
        return;
    }
    mText.Append(mShowOrbitLines ? TEXT("ORBITS ON (O)") : TEXT("ORBITS OFF (O)"));
    mText.Append(TEXT("   F3 speed panel   J jump map   T/R/F/X target   M match frame   [ ] warp"));
    mText.Append(TEXT("   - = radar zoom   Home radar auto"));
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
        mText.Append(TEXT(" [M LOCK]"));
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

    // Flight-assist and boost
    mText.Reset();
    mText.Append(control.bFlightAssist ? TEXT("ASSIST ON") : TEXT("ASSIST OFF (Newtonian)"));
    mText.Append(control.bBoost ? TEXT("   BOOST") : TEXT("   boost off"));
    DrawBuffer(ESOLHudLine::Assist, control.bFlightAssist ? HUD_TEXT_COLOR : HUD_WARN_COLOR, x, y, small);
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
        mText.Append(TEXT("none (T)"));
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
// Samples every body's orbit ellipse (parent-relative) into the reused point buffers
void ASOLFlightHud::RefreshOrbitCache()
{
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    const double centuries = SimClock->GetClock().GetCenturiesSinceJ2000();
    const int32 bodyCount = FMath::Min(registry.Num(), mOrbitPoints.Num());
    for (int32 index = 0; index < bodyCount; ++index)
    {
        mOrbitPoints[index].Reset();
        if (registry.GetParent(index) != INDEX_NONE)
        {
            SOLOrbitLines::SampleEllipse(registry.GetElements(index).AtCenturies(centuries), mOrbitPoints[index]);
        }
    }
    mOrbitSampledAtS = GetWorld()->GetRealTimeSeconds();
    mHasOrbitSamples = true;
}

//////////////////////////////////////////////////////////////////////////
// Draws every body's orbit ellipse, clipped at the near plane and to the screen
void ASOLFlightHud::DrawOrbitLines(const FSOLHudView& view)
{
    if (!mHasOrbitSamples || GetWorld()->GetRealTimeSeconds() - mOrbitSampledAtS >= HUD_ORBIT_REFRESH_S)
    {
        RefreshOrbitCache();
    }
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    const int32 selectedIndex = Targeting != nullptr ? Targeting->GetSelectedIndex() : INDEX_NONE;
    const int32 bodyCount = FMath::Min(registry.Num(), mOrbitPoints.Num());

    // Each point is parent-relative: parent's universe position + point, placed like a body, then camera-local
    for (int32 index = 0; index < bodyCount; ++index)
    {
        const TArray<FVector3d>& points = mOrbitPoints[index];
        if (points.Num() < 2)
        {
            continue;
        }
        const FVector3d& parentM = registry.GetPositionM(registry.GetParent(index));
        const FLinearColor& color = index == selectedIndex ? HUD_ORBIT_TARGET_COLOR : HUD_ORBIT_COLOR;
        FVector previous = view.Rotation.UnrotateVector(
            AnchorSubsystem->ComputePointRenderLocationCm(parentM + points[0]) - view.Location);
        for (int32 pointIndex = 1; pointIndex < points.Num(); ++pointIndex)
        {
            const FVector next = view.Rotation.UnrotateVector(
                AnchorSubsystem->ComputePointRenderLocationCm(parentM + points[pointIndex]) - view.Location);
            mLastOrbitSegmentsDrawn += DrawLocalSegment(view, previous, next, color) ? 1 : 0;
            previous = next;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Draws one 3D segment given in camera-local cm, clipped at the near plane and to the canvas; true if drawn
bool ASOLFlightHud::DrawLocalSegment(const FSOLHudView& view, FVector localA, FVector localB,
    const FLinearColor& color)
{
    // Near-plane clip: a segment with no endpoint strictly in front of the plane is dropped, so past this test the
    // other endpoint of any X < near endpoint has X > near. The clip ratio (near - p.X) / (q.X - p.X) then has
    // 0 < numerator < denominator, so it lies in (0, 1) by construction; the clipped X is pinned to the plane
    if (localA.X <= HUD_NEAR_PLANE_CM && localB.X <= HUD_NEAR_PLANE_CM)
    {
        return false;
    }
    if (localA.X < HUD_NEAR_PLANE_CM)
    {
        localA = localA + (localB - localA) * ((HUD_NEAR_PLANE_CM - localA.X) / (localB.X - localA.X));
        localA.X = HUD_NEAR_PLANE_CM;
    }
    else if (localB.X < HUD_NEAR_PLANE_CM)
    {
        localB = localB + (localA - localB) * ((HUD_NEAR_PLANE_CM - localB.X) / (localA.X - localB.X));
        localB.X = HUD_NEAR_PLANE_CM;
    }
    FVector2D screenA;
    FVector2D screenB;
    if (!ProjectLocal(view, localA, screenA) || !ProjectLocal(view, localB, screenB))
    {
        return false;
    }

    // Screen clip so a segment that ends far off-screen is not handed to the canvas with huge coordinates
    if (!FlightHudClipSegment(screenA, screenB, FVector2D::ZeroVector, FVector2D(Canvas->ClipX, Canvas->ClipY)))
    {
        return false;
    }
    DrawLine(screenA.X, screenA.Y, screenB.X, screenB.Y, color, 1.0f);
    return true;
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
