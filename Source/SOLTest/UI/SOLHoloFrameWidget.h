/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "UI/SOLHoloFrameBuild.h"
#include "UI/SOLHoloMenuMath.h"

#include "CoreMinimal.h"
#include "Components/Widget.h"

#include "SOLHoloFrameWidget.generated.h"

class SSOLHoloFrame;
class USOLHoloMenuSubsystem;

/**
 * The UMG wrapper of SSOLHoloFrame (SDD 42 section 4.3): a C++ UMG menu places it behind its content (filling the
 * menu's panel slot), sets its layout and hovered item in the frame's local units, and calls BeginOpen / BeginClose.
 * The open and hover cues are requested here through USOLHoloMenuSubsystem::PlayCue (at any strength, D21); the select
 * cue is the menu's call (PlaySelectCue). The frame draws itself in paint; this wrapper has no tick: a closing menu
 * polls AdvanceAnim from its own NativeTick and removes itself once it reads Closed (SDD 4.5). The anim lives in the
 * Slate widget, so a rebuilt widget starts Closed (the layout is kept and pushed again). Always hit-test invisible.
 */
UCLASS()
class SOLTEST_API USOLHoloFrameWidget : public UWidget
{
    GENERATED_BODY()

public:

    // Makes the widget hit-test invisible (a frame never takes the mouse) and volatile (it animates in paint)
    USOLHoloFrameWidget();

    // Sets the layout in the frame's local units (title step, dividers, tab markers; see SSOLHoloFrame::SetLayout)
    void SetLayout(const FSOLHoloFrameSpec& localSpec);

    // Sets the hovered item (INDEX_NONE: none), its rect in the frame's local units and its mark; requests the hover
    // cue when one is due (a new item while Open, rate limited)
    void SetHoveredItem(int32 item, const FBox2f& localRect, ESOLHoloHoverMark mark);

    // Starts the open (or reverses a close) and requests the open cue when it left Closed or Closing; returns that
    bool BeginOpen();

    // Starts the close from the current progress; returns true when a close started
    bool BeginClose();

    // Requests the select cue (an enabled item confirmed)
    void PlaySelectCue();

    // Advances the anim to now and returns its phase (Closed when the Slate widget does not exist)
    ESOLHoloMenuPhase AdvanceAnim();

    // Returns the look now (fill alpha for the content's render opacity; hidden when the Slate widget does not exist)
    FSOLHoloMenuLook GetLook() const;

    // Returns an item's rect in the frame's local units from the item's and the frame's cached geometry
    FBox2f ToFrameLocal(const FGeometry& itemGeometry) const;

    // Returns the Slate frame, or null before it is built (verification)
    TSharedPtr<SSOLHoloFrame> GetFrame() const { return mFrame; }

    // Drops the Slate frame
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:

    // Creates the Slate frame and gives it the subsystem and the kept layout
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:

    // Returns the world's hologram menu subsystem, or null (designer, no world)
    USOLHoloMenuSubsystem* FindSubsystem() const;

    TSharedPtr<SSOLHoloFrame> mFrame;
    TWeakObjectPtr<USOLHoloMenuSubsystem> mSubsystem;   // Cues; found at RebuildWidget
    FSOLHoloFrameSpec mLayout;                          // Kept for a rebuilt Slate frame
};
