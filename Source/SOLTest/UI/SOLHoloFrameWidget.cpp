/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHoloFrameWidget.h"

#include "UI/SOLHoloFrame.h"
#include "UI/SOLHoloMenuSubsystem.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"

//////////////////////////////////////////////////////////////////////////
// Makes the widget hit-test invisible and volatile
USOLHoloFrameWidget::USOLHoloFrameWidget()
{
    SetVisibilityInternal(ESlateVisibility::HitTestInvisible);

    // SynchronizeProperties applies bIsVolatile to the Slate frame and would undo SSOLHoloFrame's own
    // ForceVolatile(true): an invalidation panel would then freeze the anim, the sweep and the hover
    ForceVolatile(true);
}

//////////////////////////////////////////////////////////////////////////
// Keeps the layout and hands it to the Slate frame
void USOLHoloFrameWidget::SetLayout(const FSOLHoloFrameSpec& localSpec)
{
    mLayout = localSpec;
    if (mFrame.IsValid())
    {
        mFrame->SetLayout(localSpec);
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets the hovered item and requests the hover cue when one is due
void USOLHoloFrameWidget::SetHoveredItem(const int32 item, const FBox2f& localRect, const ESOLHoloHoverMark mark,
    const bool bAllowCue)
{
    if (!mFrame.IsValid())
    {
        return;
    }
    const double nowS = FPlatformTime::Seconds();
    USOLHoloMenuSubsystem* subsystem = mSubsystem.Get();
    if (mFrame->SetHoveredItem(item, localRect, mark, nowS, bAllowCue) && subsystem != nullptr)
    {
        subsystem->PlayCue(ESOLHoloCue::Hover, nowS);
    }
}

//////////////////////////////////////////////////////////////////////////
// Starts the open and requests the open cue when it left Closed or Closing
bool USOLHoloFrameWidget::BeginOpen()
{
    if (!mFrame.IsValid())
    {
        return false;
    }
    const double nowS = FPlatformTime::Seconds();
    const bool bIsOpening = mFrame->BeginOpen(nowS);
    USOLHoloMenuSubsystem* subsystem = mSubsystem.Get();
    if (bIsOpening && subsystem != nullptr)
    {
        subsystem->PlayCue(ESOLHoloCue::Open, nowS);
    }
    return bIsOpening;
}

//////////////////////////////////////////////////////////////////////////
// Starts the close from the current progress
bool USOLHoloFrameWidget::BeginClose()
{
    return mFrame.IsValid() && mFrame->BeginClose(FPlatformTime::Seconds());
}

//////////////////////////////////////////////////////////////////////////
// Drops the anim to Closed at once
void USOLHoloFrameWidget::ResetClosed()
{
    if (mFrame.IsValid())
    {
        mFrame->ResetClosed();
    }
}

//////////////////////////////////////////////////////////////////////////
// Requests the select cue
void USOLHoloFrameWidget::PlaySelectCue()
{
    if (USOLHoloMenuSubsystem* subsystem = mSubsystem.Get())
    {
        subsystem->PlayCue(ESOLHoloCue::Select, FPlatformTime::Seconds());
    }
}

//////////////////////////////////////////////////////////////////////////
// Advances the anim to now and returns its phase
ESOLHoloMenuPhase USOLHoloFrameWidget::AdvanceAnim()
{
    return mFrame.IsValid() ? mFrame->Advance(FPlatformTime::Seconds()) : ESOLHoloMenuPhase::Closed;
}

//////////////////////////////////////////////////////////////////////////
// Returns the look now
FSOLHoloMenuLook USOLHoloFrameWidget::GetLook() const
{
    return mFrame.IsValid() ? mFrame->EvaluateLook(FPlatformTime::Seconds()) : FSOLHoloMenuLook();
}

//////////////////////////////////////////////////////////////////////////
// Returns an item's rect in the frame's local units (both geometries as last arranged)
FBox2f USOLHoloFrameWidget::ToFrameLocal(const FGeometry& itemGeometry) const
{
    const FGeometry& frameGeometry = GetCachedGeometry();
    const FVector2f topLeft(frameGeometry.AbsoluteToLocal(itemGeometry.LocalToAbsolute(FVector2D::ZeroVector)));
    const FVector2f bottomRight(frameGeometry.AbsoluteToLocal(itemGeometry.LocalToAbsolute(
        itemGeometry.GetLocalSize())));
    return FBox2f(topLeft, bottomRight);
}

//////////////////////////////////////////////////////////////////////////
// Drops the Slate frame
void USOLHoloFrameWidget::ReleaseSlateResources(const bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    mFrame.Reset();
}

//////////////////////////////////////////////////////////////////////////
// Creates the Slate frame and gives it the subsystem and the kept layout
TSharedRef<SWidget> USOLHoloFrameWidget::RebuildWidget()
{
    mSubsystem = FindSubsystem();
    mFrame = SNew(SSOLHoloFrame);
    mFrame->SetMenuSubsystem(mSubsystem.Get());
    mFrame->SetLayout(mLayout);
    return mFrame.ToSharedRef();
}

//////////////////////////////////////////////////////////////////////////
// Returns the world's hologram menu subsystem, or null
USOLHoloMenuSubsystem* USOLHoloFrameWidget::FindSubsystem() const
{
    const UWorld* world = GetWorld();
    return world != nullptr ? world->GetSubsystem<USOLHoloMenuSubsystem>() : nullptr;
}
