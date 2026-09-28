/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLDebugHUD.h"

#include "Game/SOLSpectatorPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace
{
    constexpr float HUD_MARGIN_PX = 16.0f;
    constexpr float HUD_LINE_HEIGHT_PX = 20.0f;
    constexpr double HUD_KM_DISPLAY_LIMIT_M = 1.0e10;   // Above this, distances are shown in AU
    constexpr double HUD_KMPS_DISPLAY_LIMIT_MPS = 1.0e7; // Above this, speeds are shown as a fraction of c

    //////////////////////////////////////////////////////////////////////////
    // Formats a distance in m, km or AU
    FString FormatDistance(const double meters)
    {
        const double magnitude = FMath::Abs(meters);
        if (magnitude < SOL::METERS_PER_KM)
        {
            return FString::Printf(TEXT("%.1f m"), meters);
        }
        if (magnitude < HUD_KM_DISPLAY_LIMIT_M)
        {
            return FString::Printf(TEXT("%.1f km"), meters / SOL::METERS_PER_KM);
        }
        return FString::Printf(TEXT("%.4f AU"), meters / SOL::AU_M);
    }

    //////////////////////////////////////////////////////////////////////////
    // Formats a speed in m/s, km/s or c
    FString FormatSpeed(const double mps)
    {
        if (mps < SOL::METERS_PER_KM)
        {
            return FString::Printf(TEXT("%.1f m/s"), mps);
        }
        if (mps < HUD_KMPS_DISPLAY_LIMIT_MPS)
        {
            return FString::Printf(TEXT("%.2f km/s"), mps / SOL::METERS_PER_KM);
        }
        return FString::Printf(TEXT("%.4f c"), mps / SOL::SPEED_OF_LIGHT_MPS);
    }

    //////////////////////////////////////////////////////////////////////////
    // Formats a warp factor as a multiplier or as sim time per real second
    FString FormatWarp(const double factor)
    {
        if (factor >= SOL::SECONDS_PER_DAY)
        {
            return FString::Printf(TEXT("%.0f d/s"), factor / SOL::SECONDS_PER_DAY);
        }
        if (factor >= SOL::SECONDS_PER_HOUR)
        {
            return FString::Printf(TEXT("%.0f h/s"), factor / SOL::SECONDS_PER_HOUR);
        }
        return FString::Printf(TEXT("%.0fx"), factor);
    }
}

//////////////////////////////////////////////////////////////////////////
// Caches the universe, ship and targeting subsystems
void ASOLDebugHUD::BeginPlay()
{
    Super::BeginPlay();
    const UWorld* world = GetWorld();
    SimClock = world->GetSubsystem<USOLSimClockSubsystem>();
    BodyRegistry = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    AnchorSubsystem = world->GetSubsystem<USOLAnchorSubsystem>();
    Ships = world->GetSubsystem<USOLShipSubsystem>();
    Targeting = world->GetSubsystem<USOLTargetingSubsystem>();
}

//////////////////////////////////////////////////////////////////////////
// Draws the debug readout lines
void ASOLDebugHUD::DrawHUD()
{
    Super::DrawHUD();
    if (SimClock == nullptr || BodyRegistry == nullptr || AnchorSubsystem == nullptr || GEngine == nullptr)
    {
        return;
    }
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();

    // Temporary debug text: string building per frame is acceptable here only because this HUD is replaced in 1c
    TArray<FString, TInlineAllocator<12>> lines;
    lines.Add(FString::Printf(TEXT("Sim UTC  %s   warp %s   ([ ] step, Backspace reset)"),
        *SimClock->GetUtcDateTime().ToString(TEXT("%Y-%m-%d %H:%M:%S")),
        *FormatWarp(SimClock->GetClock().GetWarpFactor())));
    const int32 anchorIndex = AnchorSubsystem->GetAnchorIndex();
    double altitudeM = 0.0;
    const int32 nearest = AnchorSubsystem->FindNearestBody(altitudeM);
    lines.Add(FString::Printf(TEXT("Nearest  %s   altitude %s   anchor %s"),
        nearest == INDEX_NONE ? TEXT("none") : *registry.GetName(nearest).ToString(), *FormatDistance(altitudeM),
        anchorIndex == INDEX_NONE ? TEXT("none") : *registry.GetName(anchorIndex).ToString()));

    // Ship: speed relative to the active frame (large) and absolute Sun-frame speed, cap, assist, boost
    if (Ships != nullptr && Ships->HasPlayerShip() && Targeting != nullptr)
    {
        const FSOLShipState state = Ships->GetState();
        const FSOLShipControl control = Ships->GetControl();
        lines.Add(FString::Printf(TEXT("Speed  %s  rel %s%s   (abs %s)"),
            *FormatSpeed((state.VelocityMps - Ships->GetReferenceVelocityMps()).Size()),
            *Targeting->GetFrameName().ToString(), Targeting->IsFrameLocked() ? TEXT(" [M LOCK]") : TEXT(""),
            *FormatSpeed(state.VelocityMps.Size())));
        lines.Add(FString::Printf(TEXT("Cap  %s (wheel)   assist %s (Tab)   boost %s (Shift)"),
            *FormatSpeed(control.SpeedCapMps), control.bFlightAssist ? TEXT("ON") : TEXT("OFF"),
            control.bBoost ? TEXT("ON") : TEXT("off")));
        if (const FSOLTargetInfo* target = Targeting->GetSelectedTarget())
        {
            lines.Add(FString::Printf(TEXT("Target  %s   distance %s   rel speed %s   (T R F X)"),
                *target->Name.ToString(), *FormatDistance(FVector3d::Dist(target->PositionM, state.PositionM)),
                *FormatSpeed((state.VelocityMps - target->VelocityMps).Size())));
        }
        else
        {
            lines.Add(TEXT("Target  none   (T select, R/F cycle, X clear, M lock frame)"));
        }
    }
    else if (const ASOLSpectatorPawn* spectator = Cast<ASOLSpectatorPawn>(GetOwningPawn()))
    {
        lines.Add(FString::Printf(TEXT("Speed  %s   cap %s   (wheel)"), *FormatSpeed(spectator->GetCurrentSpeedMps()),
            *FormatSpeed(spectator->GetSpeedCapMps())));
    }
    const FVector3d positionAU = AnchorSubsystem->GetObserverPositionM() / SOL::AU_M;
    lines.Add(FString::Printf(TEXT("Universe (AU, ecliptic)  %.6f  %.6f  %.6f"), positionAU.X, positionAU.Y,
        positionAU.Z));

    UFont* font = GEngine->GetMediumFont();
    float y = HUD_MARGIN_PX;
    for (const FString& line : lines)
    {
        DrawText(line, FLinearColor::White, HUD_MARGIN_PX, y, font);
        y += HUD_LINE_HEIGHT_PX;
    }
}
