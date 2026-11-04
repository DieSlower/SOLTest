/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Interaction/SOLInteractMenus.h"

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "SOLActionListWidget.generated.h"

class UBorder;
class UOverlay;
class UTextBlock;
class UVerticalBox;
class USOLHoloFrameWidget;
class USOLInteractionSubsystem;

/**
 * The map's interaction list menu (SDD 22 section 2.4): the action table of one object as a hologram list opened at
 * the cursor, one row per action ("Move To", or a disabled row dimmed with its reason). Up / Down move the highlight
 * (SOLListMenu, wrapping, disabled rows included so their reason shows), Enter or a click on an enabled row runs it
 * through USOLInteractionSubsystem::Execute and closes, Esc or a click outside the rows closes. The owner (the ship
 * pawn) gives it UI-only focus and is told through the close delegate.
 *
 * It is a hologram menu (SDD 42 part 42g, D16): a USOLHoloFrameWidget behind the content draws the Overlay Menu's
 * option (c) frame with the faint tint (D20): the title step sized to the title, the hint divider, and the hover mark
 * (brackets and pointer) around the highlighted row; the content's render opacity follows the frame's fill. It opens
 * with a draw-in and plays the open cue, the hover cue on each highlight change and the select cue when an enabled
 * action is chosen (none on a disabled row or a cancel). BeginClose gives input back at once: the widget turns hit-test
 * invisible and unfocusable, plays the close and removes itself from its NativeTick (SDD 4.5); Open during the close
 * reverses it. The NativeTick does nothing while the list is open and idle.
 */
UCLASS()
class SOLTEST_API USOLActionListWidget : public UUserWidget
{
    GENERATED_BODY()

public:

    // Fills the list for an object and places it at the cursor (viewport pixels); false when the object has no actions.
    // Opens the frame (or reverses a close still playing)
    bool Open(USOLInteractionSubsystem* interaction, const FSOLObjectRef& object, const FVector2D& cursorPx,
        const FVector2D& viewportSizePx, const FSimpleDelegate& close);

    // Starts the non-blocking close (SDD 4.5): hit-test invisible and unfocusable at once, the frame's close plays,
    // then the widget removes itself (at once when nothing is left to draw, e.g. at strength 0)
    void BeginClose();

    // Removes the list at once without the close (teardown, map close); the next Open draws in again with its open cue
    void Dismiss();

    // Returns whether the close is playing or done and the list not reopened since
    bool IsClosing() const { return mIsClosing; }

    // Returns the hologram frame (verification)
    USOLHoloFrameWidget* GetHoloFrame() const { return HoloFrame; }

    // Returns the content's render opacity (the frame's fill alpha, verification)
    float GetContentOpacity() const { return mContentOpacity; }

    // Runs a row's action when it is enabled and closes the list with the select cue; returns whether it ran (clicks,
    // Enter, verification). Nothing while the list closes
    bool ConfirmRow(int32 row);

    // Returns the object the list is for
    const FSOLObjectRef& GetObject() const { return mObject; }

    // Returns the list's rows and highlight
    const FSOLListMenuState& GetState() const { return mState; }

    // Returns the actions as they were when the list opened
    const FSOLActionList& GetActions() const { return mActions; }

    // Moves the highlight (Up / Down; verification)
    void MoveHighlight(int32 step);

    // Returns a row's centre in absolute (screen) Slate space as last laid out, or zero (verification clicks)
    FVector2D GetRowScreenCenter(int32 row) const;

protected:

    // Builds the widget tree (no widget blueprint): a full-screen transparent root with the frame and the panel inside
    virtual void NativeOnInitialized() override;

    // While the frame animates: advances it, sets the content opacity, removes the widget once closed; after a content
    // or highlight change: lays the frame out from the arranged content. Nothing while open and idle
    virtual void NativeTick(const FGeometry& geometry, float deltaSeconds) override;

    // Up / Down move the highlight, Enter runs the highlighted row, Esc closes
    virtual FReply NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent) override;

    // A left click on an enabled row runs it; a click outside the rows closes the list
    virtual FReply NativeOnMouseButtonDown(const FGeometry& geometry, const FPointerEvent& mouseEvent) override;

    // The row under the mouse is highlighted
    virtual FReply NativeOnMouseMove(const FGeometry& geometry, const FPointerEvent& mouseEvent) override;

private:

    // Returns the row under a screen-space point, or INDEX_NONE
    int32 FindRowAt(const FVector2D& screenPosition) const;

    // Re-colours the rows (highlight, disabled)
    void RefreshRows();

    // Asks the owner to close the list
    void RequestClose();

    // Asks the frame layout to be rebuilt from the arranged content over the next frames
    void MarkLayoutDirty();

    // Sets the frame's layout and hover mark from the content's arranged geometry (frame-local units)
    void UpdateFrameLayout();

    // Sets the frame's layout from the content's desired sizes on a reopen during the close (before it is arranged),
    // so the previous object's title step and hint divider never show over the new content
    void UpdateFrameLayoutFromDesired();

    // Sets the frame's hovered item: the highlighted row, its mark around the row's text
    void UpdateFrameHover();

    // Sets the content's render opacity when it changed
    void ApplyContentOpacity(float opacity);

    // Opens the built frame (or reverses its close) with the open cue
    void StartFrameOpen();

    // Ends the close: removes the widget
    void FinishClose();

    UPROPERTY(Transient)
    TObjectPtr<USOLInteractionSubsystem> Interaction;

    UPROPERTY(Transient)
    TObjectPtr<UBorder> RootBorder;                         // Full-screen transparent root (outside clicks close)

    UPROPERTY(Transient)
    TObjectPtr<UOverlay> PanelStack;                        // The frame behind the panel

    UPROPERTY(Transient)
    TObjectPtr<USOLHoloFrameWidget> HoloFrame;              // The hologram frame (D16, D20)

    UPROPERTY(Transient)
    TObjectPtr<UBorder> PanelBorder;                        // The panel (clear over the frame), its content's opacity

    UPROPERTY(Transient)
    TObjectPtr<UVerticalBox> RowColumn;                     // The rows (the frame's row column for its hint divider)

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> TitleText;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> HintText;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UBorder>> RowBorders;                 // MAX_ACTIONS rows built once (the click targets)

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> RowTexts;

    FSimpleDelegate mClose;                                 // Asks the owner to close the list
    FSOLObjectRef mObject;                                  // The object the actions are for
    FSOLActionList mActions;                                // Its actions when the list opened
    FSOLListMenuState mState;                               // Rows and highlight (SOLListMenu)
    int32 mLayoutDirtyFrames = 0;                           // Frames left to re-lay the frame out after a change
    float mContentOpacity = 1.0f;                           // The content's render opacity as last set
    ESlateVisibility mOpenVisibility = ESlateVisibility::Visible;  // The widget's visibility while open
    bool mIsClosing = false;                                // BeginClose ran and Open did not since
    bool mIsAnimating = false;                              // The frame is opening or closing
    bool mIsOpenPending = false;                            // Opened before the frame's Slate widget was built
};
