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
class UTextBlock;
class USOLInteractionSubsystem;

/**
 * The map's interaction list menu (SDD 22 section 2.4): the action table of one object as a hologram list opened at
 * the cursor, one row per action ("Move To", or a disabled row dimmed with its reason). Up / Down move the highlight
 * (SOLListMenu, wrapping, disabled rows included so their reason shows), Enter or a click on an enabled row runs it
 * through USOLInteractionSubsystem::Execute and closes, Esc or a click outside the rows closes. The owner (the ship
 * pawn) gives it UI-only focus and is told through the close delegate.
 */
UCLASS()
class SOLTEST_API USOLActionListWidget : public UUserWidget
{
    GENERATED_BODY()

public:

    // Fills the list for an object and places it at the cursor (viewport pixels); false when the object has no actions
    bool Open(USOLInteractionSubsystem* interaction, const FSOLObjectRef& object, const FVector2D& cursorPx,
        const FVector2D& viewportSizePx, const FSimpleDelegate& close);

    // Runs a row's action when it is enabled and closes the list; returns whether it ran (clicks, Enter, verification)
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

    // Builds the widget tree (no widget blueprint): a full-screen transparent root with the panel inside
    virtual void NativeOnInitialized() override;

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

    UPROPERTY(Transient)
    TObjectPtr<USOLInteractionSubsystem> Interaction;

    UPROPERTY(Transient)
    TObjectPtr<UBorder> RootBorder;                         // Full-screen transparent root (outside clicks close)

    UPROPERTY(Transient)
    TObjectPtr<UBorder> PanelBorder;                        // The dark panel behind the list

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
};
